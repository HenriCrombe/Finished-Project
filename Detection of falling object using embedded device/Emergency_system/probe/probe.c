#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"
#include "net/uip.h"
#include "net/uip-ds6.h"
#include "net/netstack.h"
#include "adxl345.h" // accelerometer
#include "dev/leds.h"
#include "dev/button-sensor.h"
#include "dev/battery-sensor.h"
#include "rest-engine.h" // for the REST server
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>


#define PROBE_ID          1

#define CONN_TIMEOUT      60
#define MAX_PAYLOAD_LEN   64

#define PROBE_STATUS_INIT       0
#define PROBE_STATUS_ALIVE      1
#define PROBE_STATUS_EMERGENCY  2
#define PROBE_STATUS_DISCONNECT 3
#define PROBE_STATUS_RESET      4

#define SERVER_STATUS_INIT_ACK       0
#define SERVER_STATUS_ALIVE          1
#define SERVER_STATUS_EMERGENCY_ACK  2
#define SERVER_STATUS_DISCONNECT_ACK 3
#define SERVER_STATUS_RESET_ACK      4

// Register id of the freefall interrupt parameters
#define THRESH_FF_REGISTER          0x28
#define TIME_FF_REGISTER            0x29

// Freefall thresholds in ms (see adxl345.h and accelerometer documentation)
// default : 160 miliseconds (see adxl345.h)
#define ACCM_FF_TIME_THRESH_50MS       0x0A
#define ACCM_FF_TIME_THRESH_100MS      0x14 
#define ACCM_FF_TIME_THRESH_200MS      0x28 
#define ACCM_FF_TIME_THRESH_300MS      0x3c
#define ACCM_FF_TIME_THRESH_400MS      0x50
#define ACCM_FF_TIME_THRESH_500MS      0x64
#define ACCM_FF_TIME_THRESH_600MS      0xF0

// Freefall thresholds for acceleration detection (300mG, 600mG and 1G)
// Default accel threshold for ADXL349 -> 600mG  (see doc)
#define ACCM_FF_THRESH_300MG         0x05
#define ACCM_FF_THRESH_600MG         0x09
#define ACCM_FF_THRESH_1G            0x10

static struct uip_udp_conn *udp_conn_to_server;
static uip_ipaddr_t serverIP;
static char buf_out[MAX_PAYLOAD_LEN];
static char buf_in[MAX_PAYLOAD_LEN];

static struct etimer global_timer;
static clock_time_t last_message_rcvd_ts; 

static bool is_connected;
static bool deconnected_safely;
static bool in_emergency_mode;

/* 
 * Define and launch processes for this mote
 */
PROCESS(probe_client, "Acceleration Probe");
PROCESS(rest_server, "REST Server");

AUTOSTART_PROCESSES(&probe_client, &rest_server);


/*-----------------------------------------------------------
 * ACCELERATION PROBE PROCESS
 *-----------------------------------------------------------
 */ 

static void 
send_packet(int status){

  buf_out[0] = (char) PROBE_ID;
  if(status == PROBE_STATUS_EMERGENCY){
    buf_out[1] = (char) PROBE_STATUS_EMERGENCY;
  }else if (status == PROBE_STATUS_INIT){
    buf_out[1] = (char) PROBE_STATUS_INIT;
  }else if (status == PROBE_STATUS_ALIVE){
    buf_out[1] = (char) PROBE_STATUS_ALIVE;
  }else if (status == PROBE_STATUS_DISCONNECT){
    buf_out[1] = (char) PROBE_STATUS_DISCONNECT;
  }else if (status == PROBE_STATUS_RESET){
    buf_out[1] = (char) PROBE_STATUS_RESET;
  }else{
    printf("Error occured during send_packet : Probe status not recognized\n");
  }

  printf("Send packet.\n");
  uip_udp_packet_send(udp_conn_to_server, buf_out, sizeof(buf_out));
}

/*
 * Handle the incoming messages from the server.
 */ 
static void
tcpip_handler(char * data){
  if(uip_newdata()) {
    printf("Processing incoming message \n");

    // Update the last received message timestamp.
    // The timestamp is used to check if the connection timeout is reached.
    last_message_rcvd_ts = clock_time();

    // Move the payload of the incoming datagram to buf_in
    int len = uip_datalen();
    memcpy(buf_in, uip_appdata, len);
    
    if(buf_in[1]==SERVER_STATUS_INIT_ACK){
      // The server confirms it has receied an INIT message
      printf("Received INIT ACK packet from server. Probe is now connected! \n");
      is_connected = true;
      in_emergency_mode = false;
      last_message_rcvd_ts = clock_time();
      // The connection between the probe and the server is now established
      send_packet(PROBE_STATUS_ALIVE);

    }else if(buf_in[1]==SERVER_STATUS_ALIVE){
      // The server responds to the probe ALIVE message
      printf("Received ALIVE packet from server. \n");
      send_packet(PROBE_STATUS_ALIVE);

    }else if(buf_in[1]==SERVER_STATUS_EMERGENCY_ACK){
      printf("Received EMERGENCY ACK from server. \n");
      leds_off(LEDS_GREEN);
      // Visually notify the user that the server acknoledge the emergency
      leds_on(LEDS_ALL);
      in_emergency_mode = true;

    }else if(buf_in[1]==SERVER_STATUS_DISCONNECT_ACK){
      // The server confirms the disconnection
      printf("Received DISCONNECT ACK from server. \n");
      is_connected=false;
      deconnected_safely =true;
      leds_off(LEDS_ALL);
      leds_on(LEDS_YELLOW);

    }else if(buf_in[1]==SERVER_STATUS_RESET_ACK){
      // The server confirms that it has correctly reset 
      printf("Received RESET ACK from server. \n");
      printf("The server is OK to reset to initial state\n");
      leds_off(LEDS_ALL);
      leds_on(LEDS_GREEN);
      in_emergency_mode = false;

    } else{
      // Unknown message type
      printf("Corrupted message received ! Sending ALIVE message back\n");
      send_packet(PROBE_STATUS_ALIVE);
    }
  }
}

/*
 * Handle the the accelerometer interrupt
 */ 
static void
freefall_callback(u8_t reg){
  printf("Free fall detected !!! \n");
  // Send emmergency messages to the server
  int i;
  for(i=0; i<5; i++){
    send_packet(PROBE_STATUS_EMERGENCY);
  }
  leds_on(LEDS_RED);
}

/*
 * Activate the different sensors when the probe is connected to the server
 */ 
static void
probe_is_connected(){
  // Activate accelerometer and button1
  leds_off(LEDS_YELLOW);
  leds_on(LEDS_GREEN);   
  SENSORS_ACTIVATE(button_sensor);

  // Init accelerometer with default values (see ADXL345.h)
  accm_init(); 
  // Register to the freefall interrupt of the accelerometer
  ACCM_REGISTER_INT1_CB((void *)freefall_callback);
  accm_set_irq(ADXL345_INT_FREEFALL,ADXL345_INT_FREEFALL);
}

/*
 * This function is called when the user purposely want to
 * disconnect the probe from the server.
 */ 
static void
disconnect_probe(){
  printf("Sending DISCONNECT message to server.\n");
  send_packet(PROBE_STATUS_DISCONNECT);
}

PROCESS_THREAD(probe_client, ev, data)
{
  printf("Starting Probe\n");
  PROCESS_BEGIN();

  SENSORS_ACTIVATE(battery_sensor);
  clock_init();
  leds_init();

  // IPv6 link address of the server is hardcoded
  uip_ip6addr(&serverIP, 0xfe80, 0, 0, 0, 0xc30c, 0, 0, 0x0001);
  // Create a new UDP connection to the server
  udp_conn_to_server = udp_new(&serverIP, UIP_HTONS(3000), NULL);

  if(!udp_conn_to_server){
    printf("udp connection to server failed.\n");
  }

  // Bind the connection to port 3001 for the server response.
  udp_bind(udp_conn_to_server, UIP_HTONS(3001));
  leds_on(LEDS_YELLOW);
  is_connected = false;
  deconnected_safely=false;

  while(1) {
    if(is_connected==false){
      // The probe is not yet connected to the server
      // Start initialization phase
      while(is_connected==false){
        if(deconnected_safely==true){
          // If the user have deconnected to mote. 
          // Don't try to reconnect until he presses the buton
          PROCESS_WAIT_EVENT_UNTIL(ev == sensors_event && data == &button_sensor);
          deconnected_safely=false;
        }
        // Try to connect every second
        // Send INIT packet to server every second 
        // until probe receive INIT_ACK packet
        send_packet(PROBE_STATUS_INIT);

        etimer_set(&global_timer, CLOCK_SECOND);
        // Wait for incoming response from the server
        PROCESS_YIELD();

        if(ev == tcpip_event){
          tcpip_handler(data);
        }else {
          printf("Probe attempt to connect : Unsucessful.\nTrying again.\n");
        }
      }
      // Probe is connected to the server.
      // Initiate accelerometer 
      probe_is_connected();

    }else {
      // Probe is connected. Process events

      etimer_set(&global_timer, CLOCK_SECOND);
      // Wait until :
      // - global timer expires (or)
      // - UDP datagram is received (or)
      // - Button is pressed -> disconnect from server (send DISCONNECT packet)
      PROCESS_YIELD();

      if(in_emergency_mode==true){
        // Freefall has been detected;
        // The probe has sent emergency packet to server
        printf("Emergency mode activated ! Press button to deactivate \n");
        PROCESS_WAIT_EVENT_UNTIL(ev == sensors_event && data == &button_sensor);
        send_packet(PROBE_STATUS_RESET);
        in_emergency_mode=false;
        printf("Emergency mode DEACTIVATED ! \n");

      }else{
        if(ev == sensors_event && data == &button_sensor){
        printf("Button has been pressed ! Disconnect from server \n");
        disconnect_probe();

        }else if(ev == tcpip_event) {
          // Server is alive. 
          // Process the response.
          tcpip_handler(data);

        }else if(etimer_expired(&global_timer)) {
          // Global time expired :
          // If connection timeout is not reached : send ALIVE message
          // If connection timeout is reached : Server is unreachable
          printf("Timer expired : Send ALIVE packet to server.\n");
          send_packet(PROBE_STATUS_ALIVE);

          clock_time_t response_delay = clock_time() - last_message_rcvd_ts ;
          unsigned short delay_sec  = response_delay/CLOCK_SECOND;
          // Check if connection timeout has been reached.
          if(delay_sec >= CONN_TIMEOUT){
            printf("Connection timeout reached ! The server is not reachable anymore.\n");
            // Display unreachability with LEDS.
            leds_off(LEDS_GREEN);
            leds_on(LEDS_YELLOW);
            leds_on(LEDS_RED);
            // Yield until the server become reachable again.
            PROCESS_YIELD();
          }
        }
      }  
    }
  }
  PROCESS_END();
}

/*-----------------------------------------------------------
 * REST SERVER PROCESS :
 * The probe hosts a REST server allowing to change 
 * and acceleration thresholds of the accelerometer.
 * The REST server is accessible with firefox thanks to the border-router 
 * process ran on the server.  (see README)
 *-----------------------------------------------------------
 */ 

static void
res_post_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  size_t len = 0;
  const char *tresh = NULL;
  if ((len = REST.get_post_variable(request, "ff_time_threshold", &tresh))) {
    printf("Received new time threshold : %d \n",atoi(tresh));

    int time_tresh = atoi(tresh);
    if(time_tresh==50){
      accm_write_reg(TIME_FF_REGISTER, ACCM_FF_TIME_THRESH_50MS);
    }else if(time_tresh==100){
      accm_write_reg(TIME_FF_REGISTER, ACCM_FF_TIME_THRESH_100MS);
    }else if(time_tresh==200){
      accm_write_reg(TIME_FF_REGISTER, ACCM_FF_TIME_THRESH_200MS);
    }else if(time_tresh==300){
      accm_write_reg(TIME_FF_REGISTER, ACCM_FF_TIME_THRESH_300MS);
    }else if(time_tresh==400){
      accm_write_reg(TIME_FF_REGISTER, ACCM_FF_TIME_THRESH_400MS);
    }else if(time_tresh==500){
      accm_write_reg(TIME_FF_REGISTER, ACCM_FF_TIME_THRESH_500MS);
    }else if(time_tresh==600){
      accm_write_reg(TIME_FF_REGISTER, ACCM_FF_TIME_THRESH_600MS);
    }else{
      // Set default threshold if the value is not recognized (160 ms)
      accm_write_reg(TIME_FF_REGISTER, ADXL345_TIME_FF_DEFAULT);
    }

  }else if((len = REST.get_post_variable(request, "ff_accel_threshold", &tresh))){
    printf("Received new acceleration threshold : %d \n",atoi(tresh));

    int accel_tresh = atoi(tresh);
    if(accel_tresh==300){
      accm_write_reg(THRESH_FF_REGISTER, ACCM_FF_THRESH_300MG);
    }else if (accel_tresh==600){
      accm_write_reg(THRESH_FF_REGISTER, ACCM_FF_THRESH_600MG);
    }else if (accel_tresh==1000){
      accm_write_reg(THRESH_FF_REGISTER, ACCM_FF_THRESH_1G);
    }else{
      // Set default threshold if the value is not recognized (563 mg)
      accm_write_reg(THRESH_FF_REGISTER, ADXL345_THRESH_FF_DEFAULT);
    }
  }
}

/* 
 * The REST server waits for incoming CoAP requests.
 * It waits for incoming message on the url :
 * IPv6_mote_addr:5683/freefall_thresholds
 *
 * The payload of the CoAP messages must contain either :
 * - ff_time_threshold=value
 * - ff_accel_threshold=value
 * where value is an acceptable threshold defined above.
 */
RESOURCE(freefall_thresholds,
         "title=\"freefall_thresholds\";Control\"",
         NULL,
         res_post_handler,
         NULL,
         NULL);

PROCESS_THREAD(rest_server, ev, data)
{
  PROCESS_BEGIN();

  printf("Starting probe REST Server\n");
  /* Initialize the REST engine et activate the ressources. */
  rest_init_engine();
  rest_activate_resource(&freefall_thresholds, "freefall_thresholds");
  // rest_activate_resource(&ff_accel_threshold, "ff_accel_threshold");

  while(1) {
    // Wait for CoAP requests
    PROCESS_WAIT_EVENT(); 
  }

  PROCESS_END();
}
