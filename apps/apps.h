#ifndef APPS_H
#define APPS_H

void app_terminal_init(void);
void app_terminal_open(void);

void app_notepad_init(void);
void app_notepad_open(void);
void app_notepad_open_file(const char* filename);

void app_calc_init(void);
void app_calc_open(void);

void app_filemanager_init(void);
void app_filemanager_open(void);

#endif
