/* cadet.c - A video4linux driver for the ADS Cadet AM/FM Radio Card 
 *
 * by Fred Gleason <fredg@wava.com>
 * Version 0.1.2
 *
 * (Loosely) based on code for the Aztech radio card by
 *
 * Russell Kroll    (rkroll@exploits.org)
 * Quay Ly
 * Donald Song
 * Jason Lewis      (jlewis@twilight.vtc.vsc.edu) 
 * Scott McGrath    (smcgrath@twilight.vtc.vsc.edu)
 * William McGrath  (wmcgrath@twilight.vtc.vsc.edu)
 *
*/

#include <linux/module.h>	/* Modules 			*/
#include <linux/init.h>		/* Initdata			*/
#include <linux/ioport.h>	/* check_region, request_region	*/
#include <linux/delay.h>	/* udelay			*/
#include <asm/io.h>		/* outb, outb_p			*/
#include <asm/uaccess.h>	/* copy to/from user		*/
#include <linux/videodev.h>	/* kernel radio structs		*/
#include <linux/config.h>	/* CONFIG_RADIO_CADET_PORT 	*/

#ifndef CONFIG_RADIO_CADET_PORT
#define CONFIG_RADIO_CADET_PORT 0x330
#endif

static int io=CONFIG_RADIO_CADET_PORT; 
static int users=0;
static int curtuner=0;

static int cadet_getstereo(void)
{
  if(curtuner!=0) {          /* Only FM has stereo capability! */
	        return 0;
	}
        outb(7,io);          /* Select tuner control */
        if((inb(io+1)&0x40)==0) {
                return 1;    /* Stereo pilot detected */
        }
        else {
                return 0;    /* Mono */
        }
}


static unsigned cadet_getfreq(void)
{
        int curvol,i;
        unsigned freq=0,test,fifo=0;
  

        /*
         * Prepare for read
         */
        outb(7,io);       /* Select tuner control */
        curvol=inb(io+1); /* Save current volume/mute setting */
        outb(0x00,io+1);  /* Ensure WRITE-ENABLE is LOW */

        /*
         * Read the shift register
         */
        for(i=0;i<25;i++) {
                fifo=(fifo<<1)|((inb(io+1)>>7)&0x01);
                if(i<24) {
                        outb(0x01,io+1);
                        outb(0x00,io+1);
                }
        }

        /*
         * Restore volume/mute setting
         */
        outb(curvol,io+1);

        /*
         * Convert to actual frequency
         */
	if(curtuner==0) {    /* FM */
	        test=12500;
                for(i=0;i<14;i++) {
                        if((fifo&0x01)!=0) {
                                freq+=test;
                        }
                        test=test<<1;
                        fifo=fifo>>1;
                }
                freq-=10700000;           /* IF frequency is 10.7 MHz */
                freq=(freq*16)/1000000;   /* Make it 1/16 MHz */
	}
	if(curtuner==1) {    /* AM */
	        freq=((fifo&0x7fff)-2010)*16;
	}

        return freq;
}


static void cadet_setfreq(unsigned freq)
{
        unsigned fifo;
        int i,test;
        int curvol;

        /* 
         * Formulate a fifo command
         */
	fifo=0;
	if(curtuner==0) {    /* FM */
        	test=102400;
                freq=(freq*1000)/16;       /* Make it kHz */
                freq+=10700;               /* IF is 10700 kHz */
                for(i=0;i<14;i++) {
                        fifo=fifo<<1;
                        if(freq>=test) {
                                fifo|=0x01;
                                freq-=test;
                        }
                        test=test>>1;
                }
	}
	if(curtuner==1) {    /* AM */
                fifo=(freq/16)+2010;            /* Make it kHz */
		fifo|=0x100000;            /* Select AM Band */
	}

        /*
         * Save current volume/mute setting
         */
        curvol=inb(io+1); 

        /*
         * Write the shift register
         */
        test=0;
        test=(fifo>>23)&0x02;      /* Align data for SDO */
        test|=0x1c;                /* SDM=1, SWE=1, SEN=1, SCK=0 */
        outb(7,io);                /* Select tuner control */
        outb(test,io+1);           /* Initialize for write */
        for(i=0;i<25;i++) {
                test|=0x01;              /* Toggle SCK High */
                outb(test,io+1);
                test&=0xfe;              /* Toggle SCK Low */
                outb(test,io+1);
                fifo=fifo<<1;            /* Prepare the next bit */
                test=0x1c|((fifo>>23)&0x02);
                outb(test,io+1);
        }
        /*
         * Restore volume/mute setting
         */
        outb(curvol,io+1);
}


static int cadet_getvol(void)
{
        outb(7,io);                /* Select tuner control */
        if((inb(io+1)&0x20)!=0) {
                return 0xffff;
        }
        else {
                return 0;
        }
}


static void cadet_setvol(int vol)
{
        outb(7,io);                /* Select tuner control */
        if(vol>0) {
                outb(0x20,io+1);
        }
        else {
                outb(0x00,io+1);
        }
}  


static int cadet_ioctl(struct video_device *dev, unsigned int cmd, void *arg)
{
        unsigned freq;
	switch(cmd)
	{
		case VIDIOCGCAP:
		{
			struct video_capability v;
			v.type=VID_TYPE_TUNER;
			v.channels=2;
			v.audios=1;
			/* No we don't do pictures */
			v.maxwidth=0;
			v.maxheight=0;
			v.minwidth=0;
			v.minheight=0;
			strcpy(v.name, "ADS Cadet");
			if(copy_to_user(arg,&v,sizeof(v)))
				return -EFAULT;
			return 0;
		}
		case VIDIOCGTUNER:
		{
			struct video_tuner v;
			if(copy_from_user(&v, arg,sizeof(v))!=0) { 
				return -EFAULT;
			}
			if((v.tuner<0)||(v.tuner>1)) {
				return -EINVAL;
			}
			switch(v.tuner) {
			        case 0:
			        strcpy(v.name,"FM");
			        v.rangelow=1400;     /* 87.5 MHz */
			        v.rangehigh=1728;    /* 108.0 MHz */
			        v.flags=0;
			        v.mode=0;
			        v.mode|=VIDEO_MODE_AUTO;
			        v.signal=0xFFFF;
			        if(cadet_getstereo()==1) {
				        v.flags|=VIDEO_TUNER_STEREO_ON;
			        }
			        if(copy_to_user(arg,&v, sizeof(v))) {
				        return -EFAULT;
			        }
			        break;
			        case 1:
			        strcpy(v.name,"AM");
			        v.rangelow=8320;      /* 520 kHz */
			        v.rangehigh=26400;    /* 1650 kHz */
			        v.flags=0;
			        v.flags|=VIDEO_TUNER_LOW;
			        v.mode=0;
			        v.mode|=VIDEO_MODE_AUTO;
			        v.signal=0xFFFF;
			        if(copy_to_user(arg,&v, sizeof(v))) {
				        return -EFAULT;
			        }
			        break;
			}
			return 0;
		}
		case VIDIOCSTUNER:
		{
			struct video_tuner v;
			if(copy_from_user(&v, arg, sizeof(v))) {
				return -EFAULT;
			}
			if((v.tuner<0)||(v.tuner>1)) {
				return -EINVAL;
			}
			curtuner=v.tuner;	
			return 0;
		}
		case VIDIOCGFREQ:
		        freq=cadet_getfreq();
			if(copy_to_user(arg, &freq, sizeof(freq)))
				return -EFAULT;
			return 0;
		case VIDIOCSFREQ:
			if(copy_from_user(&freq, arg,sizeof(freq)))
				return -EFAULT;
			if((curtuner==0)&&((freq<1400)||(freq>1728))) {
			        return -EINVAL;
			}
			if((curtuner==1)&&((freq<8320)||(freq>26400))) {
			        return -EINVAL;
			}
			cadet_setfreq(freq);
			return 0;
		case VIDIOCGAUDIO:
		{	
			struct video_audio v;
			memset(&v,0, sizeof(v));
			v.flags=VIDEO_AUDIO_MUTABLE|VIDEO_AUDIO_VOLUME;
			if(cadet_getstereo()==0) {
			        v.mode=VIDEO_SOUND_MONO;
			}
			else {
			  v.mode=VIDEO_SOUND_STEREO;
			}
			v.volume=cadet_getvol();
			v.step=0xffff;
			strcpy(v.name, "Radio");
			if(copy_to_user(arg,&v, sizeof(v)))
				return -EFAULT;
			return 0;			
		}
		case VIDIOCSAUDIO:
		{
			struct video_audio v;
			if(copy_from_user(&v, arg, sizeof(v))) 
				return -EFAULT;	
			if(v.audio) 
				return -EINVAL;
			cadet_setvol(v.volume);
			if(v.flags&VIDEO_AUDIO_MUTE) 
				cadet_setvol(0);
			else
				cadet_setvol(0xffff);
			return 0;
		}
		default:
			return -ENOIOCTLCMD;
	}
}


static int cadet_open(struct video_device *dev, int flags)
{
	if(users)
		return -EBUSY;
	users++;
	MOD_INC_USE_COUNT;
	return 0;
}

static void cadet_close(struct video_device *dev)
{
	users--;
	MOD_DEC_USE_COUNT;
}


static struct video_device cadet_radio=
{
	"Cadet radio",
	VID_TYPE_TUNER,
	VID_HARDWARE_CADET,
	cadet_open,
	cadet_close,
	NULL,	/* Can't read  (no capture ability) */
	NULL,	/* Can't write */
	NULL,	/* No poll */
	cadet_ioctl,
	NULL,
	NULL
};

__initfunc(int cadet_init(struct video_init *v))
{
#ifndef MODULE        
        if(cadet_probe()<0) {
	        return EINVAL;
	}
#endif
	if(video_register_device(&cadet_radio,VFL_TYPE_RADIO)==-1)
		return -EINVAL;
		
	request_region(io,2,"cadet");
	printk(KERN_INFO "ADS Cadet Radio Card at %x\n",io);
	return 0;
}


static int cadet_probe(void)
{
        static int iovals[8]={0x330,0x332,0x334,0x336,0x338,0x33a,0x33c,0x33e};
	int i;

	for(i=0;i<8;i++) {
	        io=iovals[i];
	        if(check_region(io,2)) {
	                return -1;
		}  
		cadet_setfreq(1410);
		if(cadet_getfreq()==1410) {
		        return io;
		}
	}
	return -1;
}



#ifdef MODULE

MODULE_AUTHOR("Fred Gleason, Russell Kroll, Quay Lu, Donald Song, Jason Lewis, Scott McGrath, William McGrath");
MODULE_DESCRIPTION("A driver for the ADS Cadet AM/FM/RDS radio card.");
MODULE_PARM(io, "i");
MODULE_PARM_DESC(io, "I/O address of Cadet card (0x330,0x332,0x334,0x336,0x338,0x33a,0x33c,0x33e)");

EXPORT_NO_SYMBOLS;

int init_module(void)
{
	if(io==-1)
	{
		printk(KERN_ERR "You must set an I/O address with io=0x???\n");
		return -EINVAL;
	}
	return cadet_init(NULL);
}

void cleanup_module(void)
{
	video_unregister_device(&cadet_radio);
	release_region(io,2);
}

#endif
