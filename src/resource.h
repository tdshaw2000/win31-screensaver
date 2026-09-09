#ifndef RESOURCE_H
#define RESOURCE_H

#define IDD_CONFIG          100
#define IDC_COLOR           101
#define IDC_BALLSIZE        102
#define IDC_BALLSIZE_VALUE  103
#define IDC_SIDES           104
#define IDC_SIDES_VALUE     105
#define IDC_SPEED           106
#define IDC_SPEED_VALUE     107

/* Control Panel's Desktop applet reads these string table entries (IDs 1
   and 2 - the conventional NAME/DESCRIPTION resource IDs from Microsoft's
   screensaver SDK) to populate the screen saver list; without a NAME
   entry it silently omits the .SCR from the list rather than falling
   back to the filename. */
#define NAME        1
#define DESCRIPTION 2

#endif /* RESOURCE_H */
