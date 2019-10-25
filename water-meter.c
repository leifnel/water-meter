#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>

#include <imgproc.h>
#include <mosquitto.h>

#include <stdbool.h>
#include <key.h>
#include <minIni.h>
#include <water-meter.h>

/* water-meter.c
Based on code by Goran Lundquist
http://forum.mysensors.org/topic/1594/automatic-water-meter-reading-with-a-webcam

2015-10-27
Updated by Jim Enkel, removed Mosquitto support/dependencies, added rrd support,
added a new (pretty pointless?) signal handler.

Dependency of libsdl1.2-dev (sudo apt-get install libsdl1.2-dev)
In the structure REGION the sizes and positions of the "Hit areas" are defined.

Usage:
./water-meter -di                    -opens a window with the webcam image for 
                                      troubleshooting. (requires X! The app will 
                                      segfault if no X server is available)
./water-meter -start_value 527234     -sets the current meter reading (510,234m^3)
                                      as the base for future total values
*/

#define NUM_REGIONS      8
#define IMAGE_WIDTH    176
#define IMAGE_HEIGHT   144 
#define WATER_METER_TOTAL_FILE   "/home/pi/water-meter/water-meter-total.log"
#define RRDTOOL_PATH             "/usr/bin/rrdtool"
#define WATER_LOG_RRD_FILE       "/home/pi/water-meter/water.rrd"

const char inifile[] = "water.ini";

typedef struct _REGION {
   int x, y;
   unsigned int w, h;
} REGION;

REGION region[] =
{
//right,down,width,height
   {109, 77, 10, 10},
   {101, 51, 10, 10},
   {110, 28, 10, 10},
   {134, 21, 10, 10},
   {157, 28, 10, 10},
   {163, 52, 10, 10},
   {157, 75, 10, 10},
   {133, 82, 10, 10}
};

unsigned int x_0 = 52;
unsigned int y_0 = 44;
unsigned int radius = 30;
unsigned int automove = 1;
   double redfactor;

Viewer *view = NULL;
Camera *cam  = NULL;
struct mosquitto *mosq = NULL;
double meter_start_value = 0.0;
bool force_print = false;

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


void onexit(int e) {
  writeini();
  cleanup(0, NULL, NULL);
  exit (e);
}

char pollkbd() {
    int c=0;;
    if (keyPressed(&c)) {
      printf("> %c ",c);
      switch (c) 
      {
       case 'e': if (y_0-radius<=0)    	break;y_0--;break; // Up
        case 'x': if (y_0+radius>=IMAGE_HEIGHT)	break;y_0++;break; // Down
        case 's': if (x_0-radius<=0)     	break;x_0--;break; // Rignt
        case 'd': if (x_0+radius>=IMAGE_WIDTH) 	break;x_0++;break; // Left
        
        case 'a': if (radius<20)		break;radius--;break; //Smaller radius
        case 'f': if (x_0-radius<=0 || y_0-radius<=0 || x_0+radius>=IMAGE_WIDTH || y_0+radius>=IMAGE_HEIGHT) break; 
                                                radius++; break; //Larger radius
        
        case 'm': automove=automove==0?1:0; break;
        case 'p':
        case 'q': break;
        default: c=0;
      }
      printf("%d %d %d\n", x_0, y_0, radius);
    }
    return c;
}



static void setregions() {
   unsigned int sqr2=radius*0.7;
   
   region[0].x=x_0;		region[0].y=y_0-radius;
   region[1].x=x_0+sqr2;        region[1].y=y_0-sqr2;
   region[2].x=x_0+radius;	region[2].y=y_0;
   region[3].x=x_0+sqr2;        region[3].y=y_0+sqr2;
   region[4].x=x_0;		region[4].y=y_0+radius;
   region[5].x=x_0-sqr2;        region[5].y=y_0+sqr2;
   region[6].x=x_0-radius;	region[6].y=y_0;
   region[7].x=x_0-sqr2;        region[7].y=y_0-sqr2;
}
static int regionHit(Image *img) {

   unsigned int i;
   unsigned int x, y;
   unsigned int rx, ry, rw, rh;
   unsigned int count_dark;
   unsigned char red;
   unsigned char green;
   unsigned char blue;
   unsigned char *pixel;
   double f;
   for (i = 0; i < NUM_REGIONS; i++) {
      count_dark = 0;
      rx = region[i].x;
      ry = region[i].y;
      rw = region[i].w;
      rh = region[i].h;

      // Count number of dark pixels in given region
      for (x = rx; x < rx + rw; x++) {
         for (y = ry; y < ry + rh; y++) {
            // Get a pointer to the current pixel
            pixel = (unsigned char *)imgGetPixel(img, x, y);

            // index 0 is blue, 1 is green and 2 is red
            red = pixel[2];
            green = pixel[1];
            blue = pixel[0];

            // check if pixel is dark
//            if (red < 128 || green < 128 || blue < 128){
            f=red/((green+blue)/redfactor);
            /*
            if (red > (green + blue )*redfactor ) {
               count_dark++;
               imgSetPixel(img, x, y, 0, 0, 255);
            } else
               imgSetPixel(img, x, y, 0, 255,0);
            */
            if (f>1.1) {
               count_dark++;
               imgSetPixel(img, x, y, 0, 0, 255);
              
            } else if (f>0.9) {
               imgSetPixel(img, x, y, 255,0,0 );
            } else if (f>0.7) {
               imgSetPixel(img, x, y, 0, 255,255 );
            } else if (f>0.5) {
               imgSetPixel(img, x, y, 60,60,60 );
             } else if (f>0.3) {
               imgSetPixel(img, x, y, 30,30,30 );
            } else {
               imgSetPixel(img, x, y, 0, 0,0 );
            }
        }
      }

      // We have a hit if more than 80% of the pixels is dark 
      if (count_dark > (rw * rh) * 0.5){
         return i;
      }
   }

   return -1;
}

static int AverageRegions(Image *img, int dispcount) {

   unsigned int i;
   unsigned int x, y;
   unsigned int rx, ry, rw, rh;
   unsigned char *pixel;
   double totred = 0;
   double totblue = 0;
   double totgreen = 0;
   double red = 0;
   double blue = 0;
   double green = 0;
   

   for (i = 0; i < NUM_REGIONS; i++) {
      rx = region[i].x;
      ry = region[i].y;
      rw = region[i].w;
      rh = region[i].h;
      red = 0;
      blue = 0;
      green = 0;

      // Count number of dark pixels in given region
      for (x = rx; x < rx + rw; x++) {
         for (y = ry; y < ry + rh; y++) {
            // Get a pointer to the current pixel
            pixel = (unsigned char *)imgGetPixel(img, x, y);

            // index 0 is blue, 1 is green and 2 is red
            red += pixel[2];
            green += pixel[1];
            blue += pixel[0];

         }
      }
      if (dispcount==1) {
        // printf("R%5.0fG%5.0fB%5.0f ",red,green,blue,(green+blue)/red);
        printf("%d %f \n",i,((green+blue)/red)/redfactor);
      };
      totred += red;
      totgreen += green;
      totblue += blue;
   
   }
   redfactor = (totgreen+totblue)/totred;
   if (dispcount==1) {
     printf("Tot:%f\n",redfactor);
   };
   return -1;
}
static void drawRegion(Image *img, REGION region, unsigned char red, unsigned char green, unsigned char blue) {

   unsigned int x, y;
   unsigned int rx, ry, rw, rh;

   rx = region.x;
   ry = region.y;
   rw = region.w;
   rh = region.h;

   y = ry;
   for (x = rx; x < rx + rw; x++) imgSetPixel(img, x, y, blue, green, red);
   y = ry + rh;
   for (x = rx; x < rx + rw; x++) imgSetPixel(img, x, y, blue, green, red);
   x = rx;
   for (y = ry; y < ry + rh; y++) imgSetPixel(img, x, y, blue, green, red);
   x = rx + rw;
   for (y = ry; y < ry + rh; y++) imgSetPixel(img, x, y, blue, green, red);
}
void doPublish(char *topic, char *payload) {

   int i;
   int  rc;

   for (i = 0; i < 10; i++) {
      rc = mosquitto_publish(mosq, NULL, topic, strlen(payload), payload, 0, true);
      if (rc != MOSQ_ERR_SUCCESS) {
         fprintf(stderr, "Error: mosquitto_publish %d. Try to reconnect.\n", rc);
         fflush(stderr);
         rc = mosquitto_reconnect(mosq);
         if (rc != MOSQ_ERR_SUCCESS) {
            fprintf(stderr, "Error: mosquitto_reconnect %d. Failed to reconnect.\n", rc);
            fflush(stderr);
         }
      }
      else {
         mosquitto_loop(mosq, 0, 1);
         return;
      }
   }
}

void publishValues(time_t time, double last_minute, double last_10minute, double last_drain,
                   double total) {
   char *last_minute_topic   = "/lusa/misc-1/WATER_METER_FLOW/status";
   char *last_10minute_topic = "/lusa/misc-1/WATER_METER_10MIN/status";
   char *last_drain_topic    = "/lusa/misc-1/WATER_METER_DRAIN/status";
   char *total_topic         = "/lusa/misc-1/WATER_METER_TOTAL/status";
   char *payload_format      = "{\"type\":\"METER_VALUE\",\"update_time\":%llu,\"value\":%.2f,\"unit\":\"%s\"}";
   char payload[200];
   static double published_last_minute   = -1.0;
   static double published_last_10minute = -1.0;
   static double published_last_drain    = -1.0;
   static double published_total         = -1.0;

   if (mosq) {
      fprintf(stderr, "Publish [");
      fflush(stderr);
      if (1 || last_minute != published_last_minute) {
         sprintf(payload, payload_format, (long long)time*1000, last_minute, "l/m");
         doPublish(last_minute_topic, payload);
         published_last_minute = last_minute;
         fprintf(stderr, "1");
         fflush(stderr);
      }
      if (last_10minute != published_last_10minute) {
         sprintf(payload, payload_format, (long long)time*1000, last_10minute, "l/10m");
         doPublish(last_10minute_topic, payload);
         published_last_10minute = last_10minute;
         fprintf(stderr, "T");
         fflush(stderr);
      }

      if (last_drain != published_last_drain && last_drain > 0.0) {
         sprintf(payload, payload_format, (long long)time*1000, last_drain, "l");
         doPublish(last_drain_topic, payload);
         published_last_drain = last_drain;
         fprintf(stderr, "D");
         fflush(stderr);
      }

      if (true || total != published_total) {
         sprintf(payload, payload_format, (long long)time*1000, total + meter_start_value, "l");
         doPublish(total_topic, payload);
         published_total = total;

         FILE *fp = fopen(WATER_METER_TOTAL_FILE, "w+");
         if (fp) {
            fprintf(fp, "%8.2f", total + meter_start_value);
            fclose(fp);
         }
         fprintf(stderr, "P");
         fflush(stderr);
      }

      mosquitto_loop(mosq, 0, 1);
      fprintf(stderr, "]\n");
      fflush(stderr);
   }
   else {
      fprintf(stderr, "Error: mosq\n");
      fflush(stderr);
   }
}

void updateValues(int new_region_number) {

   static time_t last_update_minute = 0;
   static time_t last_update_10time = 0;
   static int last_region_number = -1;

   static int    frame_rate = 0;
   static double total = 0.0;
   static double last_drain = 0.0;
   static double last_minute = 0.0;
   static double last_10minute = 0.0;

   int    elapsed_regions;
   time_t new_time = time(0);

   struct tm *tmptr = localtime(&new_time);
   char time_str[20];
   strftime(time_str, sizeof(time_str), "%H:%M:%S", tmptr);

   if (last_update_minute == 0) last_update_minute = new_time/60;
   if (last_update_10time == 0) last_update_10time = new_time;

   if (new_region_number != -1 && last_region_number != -1 &&
       new_region_number != last_region_number) {
 
      elapsed_regions = new_region_number - last_region_number;
      if (elapsed_regions < 0) elapsed_regions += NUM_REGIONS;  
      if (elapsed_regions == NUM_REGIONS-1 ) { // Dont count backwards
        elapsed_regions = 0; new_region_number = last_region_number;
      } else { 
      fprintf(stdout, "%s - Hit region: %d [ +%f l ]\n", 
      time_str,new_region_number,elapsed_regions*0.125 );
      fflush(stdout);

      total         += (elapsed_regions * 1.0 / NUM_REGIONS);
      last_minute   += (elapsed_regions * 1.0 / NUM_REGIONS);
      last_10minute += (elapsed_regions * 1.0 / NUM_REGIONS);
      last_drain    += (elapsed_regions * 1.0 / NUM_REGIONS);
      }
   }
   if (new_time/60 > last_update_minute) {
      
      publishValues(new_time, last_minute, last_10minute, last_drain, total);

      fprintf(stdout, "%s - Last minute: %6.2f l, Last 10min: %6.2f l, Last drain: %6.2f l, Total: %8.2f l, Framerate: %d Offset: %d/%d\n",
              time_str, last_minute, last_10minute, last_drain, total + meter_start_value, frame_rate/60, x_0 ,y_0);
      fflush(stdout);
   
      if (last_minute == 0.0) last_drain = 0.0;
      last_minute = 0.0;
      last_update_minute = new_time/60;
      frame_rate = 0;

      // add total value to rrd database by running shell command rrdtool update:
      char* run_command = "%s update %s N:%.2f";
      char com[100];
      sprintf(com, run_command, RRDTOOL_PATH, WATER_LOG_RRD_FILE, total + meter_start_value);
      system(com);
   }
   if(force_print){
       fprintf(stdout, "%s - Last minute: %6.2f l, Last 10min: %6.2f l, Last drain: %6.2f l, Total: %8.2f l\n",
              time_str, last_minute, last_10minute, last_drain, total + meter_start_value);
      fflush(stdout);
      force_print = false;
   }
   if (new_time >= last_update_10time + 10*60) {
      last_10minute = 0.0;
      last_update_10time = new_time;
   }
   if (new_region_number != -1) last_region_number = new_region_number;
   frame_rate++;
}

//static void cleanup(int sig, siginfo_t *siginfo, void *context) {
void cleanup(int sig, siginfo_t *siginfo, void *context) {

   if (view) viewClose(view);
   if (cam) camClose(cam);

   // unintialise the library
   quit_imgproc();

   // cleanup mosquitto connection
   if (mosq) mosquitto_destroy(mosq);
   mosquitto_lib_cleanup();

   onexit(0);
}

static void sig_handler(int signo) {
  if (signo == SIGINT){
     force_print = true;
    } 
}

   unsigned int startpx[IMAGE_WIDTH];
   unsigned int len[IMAGE_WIDTH];

static void calibrate(Image *img, int dispcount) 
{
   unsigned int x, y, p;
   double totred = 0;
   double totblue = 0;
   double totgreen = 0;
   unsigned char red,green,blue;
   unsigned char *pixel;
   unsigned int b,e,m;
   unsigned int startpy[IMAGE_HEIGHT];
   unsigned int endpy[IMAGE_HEIGHT];
   
 /*  Image *img = camGrabImage(cam);
   img = camGrabImage(cam);
   img = camGrabImage(cam);
    if (!img) {
      fprintf(stderr, "Unable to grab image\n");
      fflush(stderr);
      exit(1);
   }
*/
   if (true) {AverageRegions(img,dispcount);} else {
   // Find average ratio of red to (green+blue)
   for (x = 0; x < IMAGE_WIDTH; x++)  {
     for (y = 0; y < IMAGE_HEIGHT; y++) {
            // Get a pointer to the current pixel
            pixel = (unsigned char *)imgGetPixel(img, x, y);

            // index 0 is blue, 1 is green and 2 is red
            totred += pixel[2];
            totgreen += pixel[1];
            totblue += pixel[0];

     
     }
   }
   //printf("Red: %f Blue: %f Greem; %f \n",totred,totgreen,totblue);
   redfactor = (totgreen+totblue)/totred;
   //printf("Redfactor = %f\n",redfactor);
   };
   if (!automove) return;

   // Mark start and end of red each vertocal line of red pixels
   // At least 16 pixels in row,
   for (x = 0; x < IMAGE_WIDTH; x++)  {
     b=0;e=0;
     startpx[x]=99999;
     len[x]=0;
     
     for (y = 0; y < IMAGE_HEIGHT; y++) {
            
            // Get a pointer to the current pixel
            pixel = (unsigned char *)imgGetPixel(img, x, y);

            // index 0 is blue, 1 is green and 2 is red
            red = pixel[2];
            green = pixel[1];
            blue = pixel[0];
            if (red>(green+blue)*redfactor) {
               imgSetPixel(img, x, y, 0,0,255);
               if (b==0) {
                  b=y;e=0;
                  
               } else { 
                 e++;
               }
            } else {if (e>5) 
               {
                  startpx[x]=b;
                  len[x]=e;
               }
               b=0;
               e=0;
            }
            
         }
         if (startpx[x]<9999) {
//             printf("Mark fra %d til %d\n",startpx[x],startpx[x]+len[x]);
             for (p = startpx[x];p<startpx[x]+len[x];p++) {
                 imgSetPixel(img, x, p, 0,255,255);
             };
         };
//         viewDisplayImage(view, img);
   }
   for (y = 0; y < IMAGE_HEIGHT; y++) {
     startpy[y]=9999;
     endpy[y]=0;
   };
   int ay=0;
   m=0;
   for (x = 0; x < IMAGE_WIDTH; x++)  {
     if (startpx[x]<9999) 
     {
        m++;
//        printf("%d:  %d\n",n,startpx[x]);
        ay+=startpx[x]+len[x]/2;
        for (p=startpx[x];p<startpx[x]+len[x];p++) {
          if (startpy[p]>x) startpy[p]=x;
          if (endpy[p]<x) endpy[p]=x;
      };
     }
   }
   if (m>0) {ay=ay/m;
      y_0=(5.0*y_0+(ay-5))/6;
      for (x = 0; x < IMAGE_WIDTH; x++)  {
                 imgSetPixel(img, x, ay, 0,255,0);
      }
   };
   int ax=0;
   m=0;
   for (y = 0; y < IMAGE_HEIGHT; y++) {
     if(startpy[y]<9999) 
     {
        ax+=startpy[y]+endpy[y];
        m++;
     };
   };
   if (m>0) {ax=ax/m/2;
      x_0=(5.0*x_0+(ax-5))/6;
      for (y = 0; y < IMAGE_HEIGHT; y++) {
                 imgSetPixel(img, ax, y, 0,255,0);
      }
   };
   //printf("x0:%d Ax:%d y0: %d  Ay:%d\n",x_0,ax,y_0,ay);
   setregions();
   /*  
   printf("Pass2\n");
   // For each vertical line, look 8 lines to the right of the top pixel
   // Find the largest top, set this lines top to this max,
   for (x = 1; x < IMAGE_WIDTH-8; x++)  {
     m=startpx[x];
     if (m<9999) {
       for (p = x+1; p < x+8; p++) {
         if (startpx[p]>m) {m=startpx[p];}
       };
       len[x]-=m-startpx[x];
       startpx[x]=m;
       if (len[x]<=0) {
         startpx[x]=9999;
       }
         if (startpx[x]<9999) {
             printf("Mark fra %d til %d\n",startpx[x],startpx[x]+len[x]);
             for (p = startpx[x];p<startpx[x]+len[x];p++) {
                 imgSetPixel(img, x, p, 255,0,255);
             };
         };
         viewDisplayImage(view, img);
       
            
     }
   }
   sleep(5);  
   printf("Pass 3");
   // For each vertical line, look 8 lines to the left of the top pixel
   // Find the largest top, set this lines top to this max,
   for (x = IMAGE_WIDTH; x>8 ; x--)  {
     m=startpx[x];
     if (m<9999) {
       for (p = x-1; p > x-8; p--) {
         if (startpx[p]>m) {m=startpx[p];}
       };
       len[x]-=m-startpx[x];
       startpx[x]=m;
       if (len[x]<=0) {
         startpx[x]=9999;
       }
         if (startpx[x]<9999) {
             printf("Mark fra %d til %d\n",startpx[x],startpx[x]+len[x]);
             for (p = startpx[x];p<startpx[x]+len[x];p++) {
                 imgSetPixel(img, x, p, 129,0,0);
             };
         };
         viewDisplayImage(view, img);
       
            
     }
   }*/
         viewDisplayImage(view, img);
   

}

int main(int argc, char * argv[])
{
   int    i;
   int    new_region_number;
   char   *host = "192.168.1.254";
   int    port = 1883;
   int    keepalive = 120;
   bool   clean_session = true;
   bool   display_image = false;
 
//   struct sigaction sa;

//   memset(&sa, '\0', sizeof(sa));
//   sa.sa_sigaction = &cleanup;
//   sa.sa_flags = SA_SIGINFO;
//   if (sigaction(SIGINT, &sa, NULL) < 0) {
//      fprintf(stderr, "Error: sigaction\n");
//      fflush(stderr);
//      return 1;
//   }
   if (signal(SIGINT, sig_handler) == SIG_ERR) {
      fprintf(stdout, "\ncan't catch SIGINT\n");
      fflush(stderr);
   }

   // get start options
   for (i = 0; i < argc; i++) {
      if (strcmp(argv[i], "-di") == 0) { 
         display_image = true;
      }
      if (strcmp(argv[i], "-start_value") == 0) { 
         i++;
         sscanf(argv[i], "%lf", &meter_start_value);
      }
   }

   if (meter_start_value == 0.0) {
      FILE *fp = fopen(WATER_METER_TOTAL_FILE, "r");
      if (fp) {
         fscanf(fp, "%lf", &meter_start_value);
         fclose(fp);
      }
   }
   readini();
   setregions();

   // initialise mosquitto connection
   mosquitto_lib_init();
   mosq = mosquitto_new(NULL, clean_session, NULL);
   if (!mosq) {
      fprintf(stderr, "Error: Out of memory.\n");
      fflush(stderr);
      return 1;
   }

   if(mosquitto_connect(mosq, host, port, keepalive)){
      fprintf(stderr, "Unable to connect.\n");
      fflush(stderr);
      return 1;
   }
   
   // initialise the image library
   init_imgproc();

   // open the webcam 
   cam = camOpen(IMAGE_WIDTH, IMAGE_HEIGHT);
   if (!cam) {
      fprintf(stderr, "Unable to open camera\n");
      fflush(stderr);
      onexit(1);
   }

   // create a new viewer of the same resolution with a caption
   if (display_image) {
      view = viewOpen(IMAGE_WIDTH, IMAGE_HEIGHT, "WATER-METER");
      if (!view) {
         fprintf(stderr, "Unable to open view\n");
         fflush(stderr);
         onexit(1);
      }
   }
   // capture images from the webcam	
   int c=0;
   char k;
   while((k=pollkbd())!='q'){


      Image *img = camGrabImage(cam);
      if (!img) {
         fprintf(stderr, "Unable to grab image\n");
         fflush(stderr);
         onexit(1);
      }
      usleep(100000); //0.1 sec
      if (k!=0) {printf("%d\n",k);c=0;setregions();calibrate(img,1);} 
      else if (!c--) {calibrate(img,0); c=50;}

      // check if any region has a hit
      new_region_number = regionHit(img);

      // update accumulated values
      updateValues(new_region_number);

      if (display_image) {
         for (i = 0; i < NUM_REGIONS; i++) {

            unsigned char red = 0;
            unsigned char green= 255;
            unsigned char blue = 0;

            if (i == new_region_number) {
               red = 255;
               green = 0;
               blue = 0;
            }
            drawRegion(img, region[i], red, green, blue);
         }
         for (int x = 0; x < IMAGE_WIDTH; x++)  {
           if (automove) {
               if (startpx[x]<9999) {
                 for (int p = startpx[x];p<startpx[x]+len[x];p++) {
                   imgSetPixel(img, x, p, 0,255,255);
                 };
               };
            };
//     

            imgSetPixel(img, x, y_0+5, 255,0,0);
         }
         for (int y = 0; y < IMAGE_HEIGHT; y++) {
                 imgSetPixel(img, x_0+5, y,255,0,0);
         }
         // display the image to view the changes
         viewDisplayImage(view, img);
      }

      // destroy image
      imgDestroy(img);
      writeini();
   }

   // cleanup and exit
   cleanup(0, NULL, NULL);
   return 0;
}
