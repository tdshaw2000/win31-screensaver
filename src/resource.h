#ifndef RESOURCE_H
#define RESOURCE_H

#define IDD_CONFIG          100
#define IDC_COLOR           101
#define IDC_BALLSIZE        102
#define IDC_BALLSIZE_VALUE  103

/* Control Panel's Desktop applet reads these string table entries (IDs 1
   and 2 - the conventional NAME/DESCRIPTION resource IDs from Microsoft's
   screensaver SDK) to populate the screen saver list; without a NAME
   entry it silently omits the .SCR from the list rather than falling
   back to the filename. */
#define NAME        1
#define DESCRIPTION 2

#endif /* RESOURCE_H */
