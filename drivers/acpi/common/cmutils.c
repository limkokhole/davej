/******************************************************************************
 *
 * Module Name: cmutils - common utility procedures
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 R. Byron Moore
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include "acpi.h"
#include "events.h"
#include "hardware.h"
#include "namesp.h"
#include "interp.h"
#include "amlcode.h"
#include "debugger.h"


#define _COMPONENT          MISCELLANEOUS
	 MODULE_NAME         ("cmutils");


/*****************************************************************************
 *
 * FUNCTION:    Acpi_cm_valid_acpi_name
 *
 * PARAMETERS:  Character           - The character to be examined
 *
 * RETURN:      1 if Character may appear in a name, else 0
 *
 * DESCRIPTION: Check for a valid ACPI name.  Each character must be one of:
 *              1) Upper case alpha
 *              2) numeric
 *              3) underscore
 *
 ****************************************************************************/

u8
acpi_cm_valid_acpi_name (
	u32                     name)
{
	char                    *name_ptr = (char *) &name;
	u32                     i;


	for (i = 0; i < ACPI_NAME_SIZE; i++) {
		if (!((name_ptr[i] == '_') ||
			  (name_ptr[i] >= 'A' && name_ptr[i] <= 'Z') ||
			  (name_ptr[i] >= '0' && name_ptr[i] <= '9')))
		{
			return FALSE;
		}
	}


	return TRUE;
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_cm_valid_acpi_character
 *
 * PARAMETERS:  Character           - The character to be examined
 *
 * RETURN:      1 if Character may appear in a name, else 0
 *
 * DESCRIPTION: Check for a printable character
 *
 ****************************************************************************/

u8
acpi_cm_valid_acpi_character (
	char                    character)
{

	return ((u8)   ((character == '_') ||
			   (character >= 'A' && character <= 'Z') ||
			   (character >= '0' && character <= '9')));
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_cm_mutex_initialize
 *
 * PARAMETERS:  None.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create the system mutex objects.
 *
 ****************************************************************************/

ACPI_STATUS
acpi_cm_mutex_initialize (
	void)
{
	u32                     i;
	ACPI_STATUS             status;


	/*
	 * Create each of the predefined mutex objects
	 */
	for (i = 0; i < NUM_MTX; i++) {
		status = acpi_cm_create_mutex (i);
		if (ACPI_FAILURE (status)) {
			return (status);
		}
	}

	return (AE_OK);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_cm_mutex_terminate
 *
 * PARAMETERS:  None.
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Delete all of the system mutex objects.
 *
 ****************************************************************************/

void
acpi_cm_mutex_terminate (
	void)
{
	u32                     i;


	/*
	 * Delete each predefined mutex object
	 */
	for (i = 0; i < NUM_MTX; i++) {
		acpi_cm_delete_mutex (i);
	}

	return;
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_cm_create_mutex
 *
 * PARAMETERS:  Mutex_iD        - ID of the mutex to be created
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a mutex object.
 *
 ****************************************************************************/

ACPI_STATUS
acpi_cm_create_mutex (
	ACPI_MUTEX_HANDLE       mutex_id)
{
	ACPI_STATUS             status = AE_OK;


	if (mutex_id > MAX_MTX) {
		return (AE_BAD_PARAMETER);
	}


	if (!acpi_gbl_acpi_mutex_info[mutex_id].mutex) {
		status = acpi_os_create_semaphore (1, 1,
				   &acpi_gbl_acpi_mutex_info[mutex_id].mutex);
		acpi_gbl_acpi_mutex_info[mutex_id].locked = FALSE;
		acpi_gbl_acpi_mutex_info[mutex_id].use_count = 0;
	}

	return (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_cm_delete_mutex
 *
 * PARAMETERS:  Mutex_iD        - ID of the mutex to be deleted
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Delete a mutex object.
 *
 ****************************************************************************/

ACPI_STATUS
acpi_cm_delete_mutex (
	ACPI_MUTEX_HANDLE       mutex_id)
{
	ACPI_STATUS             status;


	if (mutex_id > MAX_MTX) {
		return (AE_BAD_PARAMETER);
	}


	status = acpi_os_delete_semaphore (acpi_gbl_acpi_mutex_info[mutex_id].mutex);

	acpi_gbl_acpi_mutex_info[mutex_id].mutex = NULL;
	acpi_gbl_acpi_mutex_info[mutex_id].locked = FALSE;

	return (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_cm_acquire_mutex
 *
 * PARAMETERS:  Mutex_iD        - ID of the mutex to be acquired
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Acquire a mutex object.
 *
 ****************************************************************************/

ACPI_STATUS
acpi_cm_acquire_mutex (
	ACPI_MUTEX_HANDLE       mutex_id)
{
	ACPI_STATUS             status;


	if (mutex_id > MAX_MTX) {
		return (AE_BAD_PARAMETER);
	}


	status =
		acpi_os_wait_semaphore (acpi_gbl_acpi_mutex_info[mutex_id].mutex,
				   1, WAIT_FOREVER);

	if (ACPI_SUCCESS (status)) {
		acpi_gbl_acpi_mutex_info[mutex_id].locked = TRUE;
		acpi_gbl_acpi_mutex_info[mutex_id].use_count++;
	}

	return (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_cm_release_mutex
 *
 * PARAMETERS:  Mutex_iD        - ID of the mutex to be released
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Release a mutex object.
 *
 ****************************************************************************/

ACPI_STATUS
acpi_cm_release_mutex (
	ACPI_MUTEX_HANDLE       mutex_id)
{
	ACPI_STATUS             status;


	if (mutex_id > MAX_MTX) {
		return (AE_BAD_PARAMETER);
	}


	acpi_gbl_acpi_mutex_info[mutex_id].locked = FALSE; /* Mark before unlocking */

	status =
		acpi_os_signal_semaphore (acpi_gbl_acpi_mutex_info[mutex_id].mutex, 1);

	return (status);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_cm_create_update_state_and_push
 *
 * PARAMETERS:  *Object         - Object to be added to the new state
 *              Action          - Increment/Decrement
 *              State_list      - List the state will be added to
 *
 * RETURN:      None
 *
 * DESCRIPTION: Create a new state and push it
 *
 ******************************************************************************/

ACPI_STATUS
acpi_cm_create_update_state_and_push (
	ACPI_OBJECT_INTERNAL    *object,
	u16                     action,
	ACPI_GENERIC_STATE      **state_list)
{
	ACPI_GENERIC_STATE       *state;


	/* Ignore null objects; these are expected */

	if (!object) {
		return AE_OK;
	}

	state = acpi_cm_create_update_state (object, action);
	if (!state) {
		return AE_NO_MEMORY;
	}


	acpi_cm_push_generic_state (state_list, state);
	return AE_OK;
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_cm_push_generic_state
 *
 * PARAMETERS:  List_head           - Head of the state stack
 *              State               - State object to push
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Push a state object onto a state stack
 *
 ******************************************************************************/

void
acpi_cm_push_generic_state (
	ACPI_GENERIC_STATE      **list_head,
	ACPI_GENERIC_STATE      *state)
{
	/* Push the state object onto the front of the list (stack) */

	state->common.next = *list_head;
	*list_head = state;

	return;
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_cm_pop_generic_state
 *
 * PARAMETERS:  List_head           - Head of the state stack
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Pop a state object from a state stack
 *
 ******************************************************************************/

ACPI_GENERIC_STATE *
acpi_cm_pop_generic_state (
	ACPI_GENERIC_STATE      **list_head)
{
	ACPI_GENERIC_STATE      *state;


	/* Remove the state object at the head of the list (stack) */

	state = *list_head;
	if (state) {
		/* Update the list head */

		*list_head = state->common.next;
	}

	return (state);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_cm_create_generic_state
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a generic state object.  Attempt to obtain one from
 *              the global state cache;  If none available, create a new one.
 *
 ******************************************************************************/

ACPI_GENERIC_STATE *
acpi_cm_create_generic_state (void)
{
	ACPI_GENERIC_STATE      *state;


	acpi_cm_acquire_mutex (ACPI_MTX_CACHES);

	acpi_gbl_state_cache_requests++;

	/* Check the cache first */

	if (acpi_gbl_generic_state_cache) {
		/* There is an object available, use it */

		state = acpi_gbl_generic_state_cache;
		acpi_gbl_generic_state_cache = state->common.next;
		state->common.next = NULL;

		acpi_gbl_state_cache_hits++;
		acpi_gbl_generic_state_cache_depth--;

		acpi_cm_release_mutex (ACPI_MTX_CACHES);
	}

	else {
		/* The cache is empty, create a new object */

		acpi_cm_release_mutex (ACPI_MTX_CACHES);

		state = acpi_cm_callocate (sizeof (ACPI_GENERIC_STATE));
	}

	/* Initialize */

	if (state) {
		state->common.data_type = ACPI_DESC_TYPE_STATE;
	}

	return state;
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_cm_create_update_state
 *
 * PARAMETERS:  Object              - Initial Object to be installed in the state
 *              Action              - Update action to be performed
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create an "Update State" - a flavor of the generic state used
 *              to update reference counts and delete complex objects such
 *              as packages.
 *
 ******************************************************************************/

ACPI_GENERIC_STATE *
acpi_cm_create_update_state (
	ACPI_OBJECT_INTERNAL    *object,
	u16                     action)
{
	ACPI_GENERIC_STATE      *state;


	/* Create the generic state object */

	state = acpi_cm_create_generic_state ();
	if (!state) {
		return NULL;
	}

	/* Init fields specific to the update struct */

	state->update.object = object;
	state->update.value  = action;

	return (state);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_cm_create_control_state
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a "Control State" - a flavor of the generic state used
 *              to support nested IF/WHILE constructs in the AML.
 *
 ******************************************************************************/

ACPI_GENERIC_STATE *
acpi_cm_create_control_state (
	void)
{
	ACPI_GENERIC_STATE      *state;


	/* Create the generic state object */

	state = acpi_cm_create_generic_state ();
	if (!state) {
		return NULL;
	}


	/* Init fields specific to the control struct */

	state->common.state = CONTROL_CONDITIONAL_EXECUTING;

	return (state);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_cm_delete_generic_state
 *
 * PARAMETERS:  State               - The state object to be deleted
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Put a state object back into the global state cache.  The object
 *              is not actually freed at this time.
 *
 ******************************************************************************/

void
acpi_cm_delete_generic_state (
	ACPI_GENERIC_STATE      *state)
{

	/* If cache is full, just free this state object */

	if (acpi_gbl_generic_state_cache_depth >= MAX_STATE_CACHE_DEPTH) {
		acpi_cm_free (state);
	}

	/* Otherwise put this object back into the cache */

	else {
		acpi_cm_acquire_mutex (ACPI_MTX_CACHES);

		/* Clear the state */

		MEMSET (state, 0, sizeof (ACPI_GENERIC_STATE));
		state->common.data_type = ACPI_DESC_TYPE_STATE;

		/* Put the object at the head of the global cache list */

		state->common.next = acpi_gbl_generic_state_cache;
		acpi_gbl_generic_state_cache = state;
		acpi_gbl_generic_state_cache_depth++;


		acpi_cm_release_mutex (ACPI_MTX_CACHES);
	}
	return;
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_cm_delete_generic_state_cache
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Purge the global state object cache.  Used during subsystem
 *              termination.
 *
 ******************************************************************************/

void
acpi_cm_delete_generic_state_cache (
	void)
{
	ACPI_GENERIC_STATE      *next;


	/* Traverse the global cache list */

	while (acpi_gbl_generic_state_cache) {
		/* Delete one cached state object */

		next = acpi_gbl_generic_state_cache->common.next;
		acpi_cm_free (acpi_gbl_generic_state_cache);
		acpi_gbl_generic_state_cache = next;
	}

	return;
}


/*****************************************************************************
 *
 * FUNCTION:    _Report_error
 *
 * PARAMETERS:  Module_name         - Caller's module name (for error output)
 *              Line_number         - Caller's line number (for error output)
 *              Component_id        - Caller's component ID (for error output)
 *              Message             - Error message to use on failure
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print error message from KD table
 *
 ****************************************************************************/

void
_report_error (
	char                    *module_name,
	s32                     line_number,
	s32                     component_id,
	char                    *message)
{

	debug_print (module_name, line_number, component_id, ACPI_ERROR,
			 "*** Error: %s\n", message);

}


/*****************************************************************************
 *
 * FUNCTION:    _Report_warning
 *
 * PARAMETERS:  Module_name         - Caller's module name (for error output)
 *              Line_number         - Caller's line number (for error output)
 *              Component_id        - Caller's component ID (for error output)
 *              Message             - Error message to use on failure
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print warning message from KD table
 *
 ****************************************************************************/

void
_report_warning (
	char                    *module_name,
	s32                     line_number,
	s32                     component_id,
	char                    *message)
{

	debug_print (module_name, line_number, component_id, ACPI_WARN,
			 "*** Warning: %s\n", message);

}


/*****************************************************************************
 *
 * FUNCTION:    _Report_success
 *
 * PARAMETERS:  Module_name         - Caller's module name (for error output)
 *              Line_number         - Caller's line number (for error output)
 *              Component_id        - Caller's component ID (for error output)
 *              Message             - Error message to use on failure
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print warning message from KD table
 *
 ****************************************************************************/

void
_report_success (
	char                    *module_name,
	s32                     line_number,
	s32                     component_id,
	char                    *message)
{

	debug_print (module_name, line_number, component_id, ACPI_OK,
			 "*** Success: %s\n", message);
}


/*****************************************************************************
 *
 * FUNCTION:    _Report_info
 *
 * PARAMETERS:  Module_name         - Caller's module name (for error output)
 *              Line_number         - Caller's line number (for error output)
 *              Component_id        - Caller's component ID (for error output)
 *              Message             - Error message to use on failure
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print information message from KD table
 *
 ****************************************************************************/

void
_report_info (
	char                    *module_name,
	s32                     line_number,
	s32                     component_id,
	char                    *message)
{

	debug_print (module_name, line_number, component_id, ACPI_INFO,
			 "*** Info: %s\n", message);

}

