#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>

#include <stdbool.h>
#include <key.h>

#include "minIni.h"

const char inifile[] = "water.ini";

  int radius;
  int x_0;
  int y_0;
  int automove;
  int max_x=200;
  int max_y=300;


void readini() {
  x_0 = ini_getl("camerapos", "x", 50, inifile);
  y_0 = ini_getl("camerapos", "y", 60, inifile);
  radius = ini_getl("camerapos", "radius", 39, inifile);
  automove = ini_getl("camerapos", "automove", 1, inifile);
};
  void writeini() {
  ini_putl("camerapos", "x", x_0, inifile);
  ini_putl("camerapos", "y", y_0, inifile);
  ini_putl("camerapos", "radius",radius, inifile);
  ini_putl("camerapos", "automove", automove, inifile);
  }

char pollkbd() {
    if (keyPressed(&c)) {
      printf("> %c ",c);
      switch (c) 
      {
        case 'e': if (y_0-radius<=0)     break;y_0--;break; // Up
        case 'x': if (y_0+radius>=max_y) break;y_0++;break; // Down
        case 's': if (x_0-radius<=0)     break;x_0--;break; // Rignt
        case 'd': if (x_0+radius>=max_x) break;x_0++;break; // Left
        
        case 'a': if (radius<20)	 break;radius--;break; //Smaller radius
        case 'f': if (x_0-radius<=0 || y_0-radius<=0 || x_0+radius>=max_x || y_0+radius>=max_y) break; radius++; break; //Larger radius
        
        case 'm': automove=automove==0?1:0; break;
        
        case 'q': looper = false;
      }
      printf("%d %d %d\n", x_0, y_0, radius);
      return c;
    }
    return \0;
}

int main(int argc, char * argv[])
{
  readini();  
  printf("Ready! \n");
  int c;
  bool looper=true;
  while (looper) { 
    pollkbd();
    nanosleep((const struct timespec[]){{0, 100000000L}}, NULL);
  };
  writeini();
  keyboardReset();
}
