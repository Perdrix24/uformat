/************************************************************
*						(Tab = 4)							*
*	USERDEFS.C	: Mise en place des Userdefs pour Windform.	*
*															*
*				par Jacques Delavoix, le 03/11/1996.		*
*															*
*************************************************************/

#include "windform.h"

#define TITLED_BOX		20
#define SQUARE_BUTTON	38
#define ROUNDED_BUTTON	34

int cdecl titled_box(PARMBLK *parmblock);
int cdecl square_button(PARMBLK *parmblock);
int cdecl rounded_button(PARMBLK *parmblock);


void set_user(OBJECT *addr_obj) /* RAJOUTER LES TEST DANS CETTE ROUTINE. */
{

	do							/* L'objet Racine n'est pas trait‚... */
	{
		addr_obj++;				/* ... pointe sur l'objet … traiter */
		if ((addr_obj->ob_type & 0xff) == G_BUTTON)
		{
			switch (addr_obj->ob_type >> 8)
			{
				case UNDER_B :
				case SMALL_B : /* Routine … appeler: "under_button()" : */
					set_objc(addr_obj, under_button);
					break;
				case SQUARE_BUTTON :				/* Nouveau au 21/06/96 */
					set_objc(addr_obj, square_button);
					break;
				case ROUNDED_BUTTON :
					set_objc(addr_obj, rounded_button);
					break;									
				case TITLED_BOX :					/* Nouveau au 16/10/96 */
					set_objc(addr_obj, titled_box);
					break;

/* Mettre ici d'autres "case" pour d'autres "buttons" en USERDEFS */

			}
		}
	} while ((addr_obj->ob_flags & LASTOB) == 0);
}

