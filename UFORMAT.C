/* 
 * uFormat v1.6 : program to format floppies on USB floppy drive
 *
 * Copyright (c) 2022 Claude Labelle
 *
 * made with Windform 3 from Jacques Delavoix
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <portab.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <tos.h>
#include <time.h>
#include <ext.h>
#include <cookie.h>

#include "format.c"
#include "windform.h"
#include "uformat.h"

#define RSC		"UFORMAT.RSC"
#define PRG		"UFORMAT.*"
#define BUSY_CHECK_DELAY	2			/* number of seconds for the retry delay when drive is busy */
#define READY_TIMEOUT   60				/* number of seconds for drive ready timeout */


/* Missing in my version of Pure C */
#define AP_TERM 	50 

void main(int argc, char *argv[], char *envp[]);
void end_prog(void);
void main_dialog(int);
void about_dialog(int);
void format(int disktype, diskinfo *disk);
void updatebar(int track);
void error(long errnum);
void message(char *msg);
void close_main(void);
void init_prog(void);

OBJECT *adr_menu;			/* menu address */
WINDFORM_VAR main_var;		
WINDFORM_VAR about_var;	
tBusInfo bus;
tHandle handle = NULL, oldhandle;

void main()
{
	int quit = 0, event;

	init_prog();
	menu_bar(adr_menu, 1);
	if (! init_scsi()) {
		form_alert(1, rsrc_get_string(NO_DRIVER));
		end_prog();
		return;
	}
	if (! find_usb_bus(&bus)) {
		form_alert(1, rsrc_get_string(NO_DRIVER));
		end_prog();
		return;
	}
	graf_mouse(ARROW, 0);	
	main_dialog(OPEN_DIAL);
	event = 0;
	event |= MU_TIMER;
	main_dialog(event);

	do		/* main loop */
	{
		event = get_evnt((MU_TIMER|MU_MESAG|MU_BUTTON|MU_KEYBD),NULL,2000);

		menu_keyshort(adr_menu, event, 0);

		if ((event & MU_MESAG) && buff[0] == MN_SELECTED)
		{
			if (buff[4] == M_QUIT)
			{
				close_main();
				quit = 1;
			}
			else if (buff[4] == M_ABOUT)
			{
				about_dialog(OPEN_DIAL);
			}
			menu_tnormal(adr_menu, buff[3], 1);
		}
		else if ((event & MU_MESAG) && buff[0] == AP_TERM )	
	  {
	  	close_main();
			quit = 1;
		}
		else if ((event & MU_TIMER))	/* Check for drive / diskette change */
		{
			main_dialog(event);
		}
		else
		{
			if (buff[3] > 0)						/* if w_handle > 0 .... */
			{
				if (buff[3] == main_var.w_handle)
					main_dialog(event);
				else if (buff[3] == about_var.w_handle)
					about_dialog(event);
			}
		}
	} while (quit == 0);

	if (handle)
		close_handle(handle);
	close_dialog(&main_var);
	menu_bar(adr_menu, 0);
	end_prog();
}

void main_dialog(int event)
{
	WINDFORM_VAR *ptr_var = &main_var;
	OBJECT *ptr_form = ptr_var->adr_form;
	static driveinfo drive;
	static diskinfo disk;
	char buf[8];
	int choix, disktype, rbutton;
	static int formatting = 0;
	char devname[28];
	long rc;

	if (event == OPEN_DIAL)
	{
		if (ptr_var->w_handle < 1)	
		{
			form_dial(FMD_GROW, 0, 0, 10, 10, ptr_var->w_x, ptr_var->w_y, ptr_var->w_w, ptr_var->w_h);
			open_dialog(ptr_var,"uFORMAT",0, 1);
		}
	}
	else if ((event & MU_TIMER) && !formatting)
	{
		memset(buf,0,8);
		oldhandle = handle;
		handle = find_drive(&bus, &drive);
		if (handle != oldhandle) 
		{
			close_handle(handle);
			sprintf(devname, "%s %s", drive.vendor, drive.product);
			message(devname);
		}
		if (handle == 0)
		{
			wf_change(ptr_var, F_FORMAT, DISABLED, 1);
			if (drive.asc == 0x3A) 
				message(rsrc_get_string(NO_DISKETTE));
			else
				message(rsrc_get_string(SEARCHING));			
		}
		else
		{
			if (media_changed(handle) || oldhandle == NULL) 
			{
				wf_change(ptr_var, F_FORMAT, -1, 1);
				get_capacities(handle, &disk);
				get_write_protect(handle, &disk);
				if (disk.wp)
				{
					message(rsrc_get_string(WRITE_PROTECTED));
					wf_change(ptr_var, F_FORMAT, DISABLED, 1);
				}
				if (disk.code == 0x03)
				{
					message(rsrc_get_string(NO_DISKETTE));
					wf_change(ptr_var, F_FORMAT, DISABLED, 1);
				}

				if (memcmp(buf, disk.capdescDD, 8) == 0)
				 	wf_change(ptr_var, F_720, DISABLED, 0);
				else
					wf_change(ptr_var, F_720, -1, 0);
										
				if (memcmp(buf, disk.capdescHD, 8) == 0)
				 	wf_change(ptr_var, F_144, DISABLED, 0);
				else
			 		wf_change(ptr_var, F_144, -1, 0);

				if (memcmp(disk.capdesc, disk.capdescDD, 4) == 0)
					init_radio(ptr_var, F_720);
				else if (memcmp(disk.capdesc, disk.capdescHD, 4) == 0)
				 	init_radio(ptr_var, F_144);
							
				wf_draw(ptr_var, F_720);
				wf_draw(ptr_var, F_144);
			}
		}
	}
	else
	{
		choix = windform_do(ptr_var, event);
		switch(choix) {
			case CLOSE_DIAL :
			case F_QUIT :
				close_main();
				end_prog();
				break;
			case F_FORMAT : 
				disktype = 0;
			 	rbutton = get_rbutton(ptr_form, F_720);
			 	if (rbutton == F_720)
			 		disktype = 3;
			 	else if (rbutton == F_144)
			 		disktype = 4;
				if (disktype) 
				{
					graf_mouse(BUSYBEE, 0);
					formatting = 1;
					format(disktype, &disk);
				 	formatting = 0;
					graf_mouse(ARROW, 0);
					wf_draw(ptr_var, F_720);
					wf_draw(ptr_var, F_144);
				}
				break;
			default :
				break;
		}
	}
}

void about_dialog(int event)
{
	WINDFORM_VAR *ptr_var = &about_var;
	int choix;
	
	if (event == OPEN_DIAL)
	{
		open_dialog(ptr_var, rsrc_get_string(ABOUT), 0, 1);
	}
	else
	{
		choix = windform_do(ptr_var, event);
		if (choix != 0)
		{
			switch(choix)
			{
				case A_OK :
					wf_change(ptr_var, choix, NORMAL, 1);
				case CLOSE_DIAL :
					close_dialog(ptr_var);
					break;
			}
		}
	}
}

void format(int disktype, diskinfo *disk)
{
	WINDFORM_VAR *ptr_var = &main_var;
	OBJECT *ptr_form = ptr_var->adr_form;
	long rc;
	int i, dummy;
	
	message(rsrc_get_string(FORMATTING));
	if (disktype == 3)
			rc = format_floppy(handle, disk->capdescDD, updatebar);
	else if (disktype == 4)
			rc = format_floppy(handle, disk->capdescHD, updatebar);
	else
			return;
			
 	if (rc != 0L) 
 		error(rc);
 	else
 	{
		message(rsrc_get_string(INITIALIZING));
		rc = init_floppy(handle, disktype, ptr_form[F_LABEL].ob_spec.tedinfo->te_ptext);			 	
 		if (rc != 0L) {
 			error(rc);
 		}
 		else {
 			Cconout(7);					/* "Ping" */
 			form_alert(1, rsrc_get_string(F_COMPLETE));
			message(rsrc_get_string(SUCCESS));	
		}
 	}
}

void updatebar(int track)
{
	WINDFORM_VAR *ptr_var = &main_var;
	OBJECT *ptr_form = ptr_var->adr_form;
	
	ptr_form[PROGRESS_POS].ob_width = (track + 1) * 2;
	wf_draw(ptr_var, PROGRESS_BOX);
}

void error(long errnum)
{
char msg[28];
	switch ((int)errnum) {
		case 0x02 :
			strcpy(msg, rsrc_get_string(NOT_READY));
			break;
		case 0x03 :
			strcpy(msg, rsrc_get_string(DISKETTE_ERROR));
			break;
		case 0x04 :
			strcpy(msg, rsrc_get_string(HARDWARE_ERROR));
			break;
		case 0x0B :
			strcpy(msg, rsrc_get_string(DRIVE_ABORTED));
			break;
		case 0x54 :
			strcpy(msg, rsrc_get_string(INTERFACE));
			break;
		case TIMEOUTERROR :
			strcpy(msg, rsrc_get_string(TIMEOUT));
			break;
		default : 
			sprintf(msg, rsrc_get_string(ERROR_CODE), errnum);
			break;
	}
	message(msg);
}

void message(char *msg)
{
	WINDFORM_VAR *ptr_var = &main_var;
	/* clear message field */
	set_editable(ptr_var, F_MESSAGE, "                           ", 1);
	set_editable(ptr_var, F_MESSAGE, msg, 1);
}

void close_main(void)
{
	WINDFORM_VAR *ptr_var = &main_var;

	close_dialog(ptr_var);
	form_dial(FMD_SHRINK, 0, 0, 10, 10, ptr_var->w_x, ptr_var->w_y, ptr_var->w_w, ptr_var->w_h);

}

void init_prog(void)
{
	init_gem();

	/* get program path */
	get_prg_path(PRG);
	
	
	if (load_rsc(RSC, NO_WINDOW, 0) == FALSE)
	{
		end_prog();
	}
	rsrc_gaddr(0, F_MENU, &adr_menu);

	init_windform(&main_var, F_DIALOG, 0, 0);
	init_windform(&about_var, F_ABOUT, 0, 0);

}

void end_prog(void)
{
	v_clsvwk(vdi_handle);
	v_clsvwk(aes_handle);
	rsrc_free();
	appl_exit();
	exit(0);
}
