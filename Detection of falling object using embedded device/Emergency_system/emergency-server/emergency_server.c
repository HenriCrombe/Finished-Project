#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"
#include "net/uip.h"
#include "net/uip-ds6.h"
#include "net/rpl/rpl.h"
#include "net/uip-debug.h"
#include "httpd-simple.h"
#include "cc2420.h" 

#include "net/netstack.h"
#include "dev/button-sensor.h"
#include "dev/slip.h"
#include "dev/leds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#define SERVER_ID         254

#define MAX_PAYLOAD_LEN   64
#define CONN_TIMEOUT      60

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

#define UIP_IP_BUF   ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
#define UIP_UDP_BUF  ((struct uip_udp_hdr *)&uip_buf[uip_l2_l3_hdr_len])

static struct uip_udp_conn *server_conn;
static clock_time_t last_message_rcvd_ts;
static char buf_in[MAX_PAYLOAD_LEN];
static char buf_out[MAX_PAYLOAD_LEN];

// Border router dag ID
uint16_t dag_id[] = {0x1111, 0x1100, 0, 0, 0, 0, 0, 0x0011};
// IPv6 prefix received by tunelslip. 
// Advertised IPv6 prefix to the other motes.
static uip_ipaddr_t prefix;
static uint8_t prefix_set;

static bool is_probe_connected;
static bool in_emergency_mode;
static struct etimer global_timer;
static int signal_strengh;

/* 
 * Define and launch processes for this mote
 */
PROCESS(udp_server_process, "UDP server");
PROCESS(border_router_process, "Border Router process");
PROCESS(webserver_nogui_process, "Web server");

AUTOSTART_PROCESSES(&udp_server_process,&webserver_nogui_process,&border_router_process);

/*-----------------------------------------------------------
 * Emergency system UDP server 
 *-----------------------------------------------------------
 */ 

static void
emergency_sequence_handler(){
  // Activate emmergency sequence here.
  leds_off(LEDS_ALL);
  leds_on(LEDS_RED);
  in_emergency_mode = true;
}

static void 
probe_unreachable_handler(){
  printf("Probe have became unreachable ! Launch emergency sequence !\n");
  emergency_sequence_handler();
}

/*
 * Handle a DISCONNECT message coming from the probe.
 * Emergency procedure should disengaged here.
 */ 
static void
disconnect_probe(){
  is_probe_connected = false;
  last_message_rcvd_ts = 0;
  leds_off(LEDS_ALL);
  leds_on(LEDS_YELLOW);
}

/*
 * Handle an INIT message coming from the probe
 * Emergency procedure should be engaged here.
 */ 
static void
probe_is_connected(){
  is_probe_connected = true;
  leds_off(LEDS_YELLOW);
  leds_on(LEDS_GREEN);
}

/* 
 * Handle a RESET message coming from the probe
 */
static void
reset_emmergency_system(){
  in_emergency_mode = false;
  last_message_rcvd_ts = clock_time();
  leds_off(LEDS_ALL);
  leds_on(LEDS_GREEN);
}

static void
tcpip_handler(char * data){
  if(uip_newdata()) {
    printf("Processing incoming message \n");
      
    // Update timestamp of the last message received
    last_message_rcvd_ts = clock_time();

    int len = uip_datalen();
    printf("Received packet of size : %d\n",len );

    // Set remote IP and Port for the response.
    uip_ipaddr_copy(&server_conn->ripaddr, &UIP_IP_BUF->srcipaddr);
    server_conn->rport = UIP_HTONS(3001); 

    // Move the payload of the incoming datagram to buf_in
    memcpy(buf_in, uip_appdata, len);
    // Put server ID in the response message
    buf_out[0] = SERVER_ID;

    // Get the signal strength using the RSSI value of the incoming message
    signal_strengh = cc2420_last_rssi + 55;
    printf("Signal strengh : %d \n", signal_strengh);

    if(buf_in[1]==PROBE_STATUS_INIT){
      printf("Received INIT packet from probe #%d !\n",buf_in[0]);
          
      buf_out[1] = SERVER_STATUS_INIT_ACK;
      probe_is_connected();

      printf("Sending INIT ACK packet to probe #%d !\n",buf_in[0]);
      uip_udp_packet_send(server_conn, buf_out, sizeof(buf_out));

    }else if(buf_in[1]==PROBE_STATUS_ALIVE){
      printf("Received ALIVE packet from probe #%d !\n",buf_in[0]);
      
      buf_out[1] = SERVER_STATUS_ALIVE;

      printf("Send ALIVE packet to probe #%d.\n",buf_in[0]);
      uip_udp_packet_send(server_conn, buf_out, sizeof(buf_out));

    }else if(buf_in[1]==PROBE_STATUS_EMERGENCY){
      printf("EMERGENCY packet received from probe #%d !\n",buf_in[0]);

      //Initiate emergency sequence
      emergency_sequence_handler();
      buf_out[1] = SERVER_STATUS_EMERGENCY_ACK;
      uip_udp_packet_send(server_conn, buf_out, sizeof(buf_out));

    }else if(buf_in[1]==PROBE_STATUS_DISCONNECT){
      printf("Probe want to disconnect safely. Send DISCONNECT_ACK to probe #%d !\n",buf_in[0]);

      buf_out[1] = SERVER_STATUS_DISCONNECT_ACK;
      uip_udp_packet_send(server_conn, buf_out, sizeof(buf_out));

      disconnect_probe();
    }else if(buf_in[1]==PROBE_STATUS_RESET){
      printf("Probe want to reset emmergency system. Send RESET_ACK to probe #%d !\n",buf_in[0]);

      reset_emmergency_system();
      buf_out[1] = SERVER_STATUS_RESET_ACK;
      uip_udp_packet_send(server_conn, buf_out, sizeof(buf_out));
    }
    /* Restore server connection to allow data from any node */
    /* The server should be able to receive message from the probe and from the Internet too */
    uip_create_unspecified(&server_conn->ripaddr);
    server_conn->rport = 0;
  }
}

PROCESS_THREAD(udp_server_process, ev, data)
{
  PROCESS_BEGIN();
  
  // Init server state and sensors.
  clock_init();
  leds_init();
  is_probe_connected = false;
  in_emergency_mode = false;

  printf("Starting UDP Server\n");
  server_conn = udp_new(NULL, UIP_HTONS(0), NULL);
  udp_bind(server_conn, UIP_HTONS(3000));

  printf("Listen port: 3000, TTL=%u\n", server_conn->ttl);

  // Wait for an incoming probe connection.
  // The probe initiate the connection with an INIT packet.
  leds_on(LEDS_YELLOW);
  while(1) {

    if(is_probe_connected==true){
      // Set a connection timeout if a probe is connected.
      // The timer is reset every time it receives a packet.
      etimer_set(&global_timer, CLOCK_SECOND*CONN_TIMEOUT);  
    }
    // Wait for incoming message/connection timeout reached
    PROCESS_YIELD();
    if(ev == tcpip_event){
      // Message received; 
      tcpip_handler(data);
    }else if(etimer_expired(&global_timer)){
      if(is_probe_connected==true){
        if(in_emergency_mode==true){
          // The emergency sequence has been activated
          // Yield for 
          // - Probe RESET packet OR
          // - Manual reset 
          PROCESS_YIELD();
        }else {
          // The probe was connected but is unreachable now.
          // Launch the corresponding emergency function.
          probe_unreachable_handler();
        }        
      }else {
        // Probe has been smoothly disconnected
        // Yield for a new probe connection
        PROCESS_YIELD();
      }
    }
  }
  PROCESS_END();
}


/*-----------------------------------------------------------
 * Functions required for tunslip
 *-----------------------------------------------------------
 */ 

void
request_prefix(void)
{
  /* mess up uip_buf with a dirty request... */
  uip_buf[0] = '?';
  uip_buf[1] = 'P';
  uip_len = 2;
  slip_send();
  uip_len = 0;
}

void
set_prefix_64(uip_ipaddr_t *prefix_64)
{
  uip_ipaddr_t ipaddr;
  memcpy(&prefix, prefix_64, 16);
  memcpy(&ipaddr, prefix_64, 16);
  prefix_set = 1;
  uip_ds6_set_addr_iid(&ipaddr, &uip_lladdr);
  uip_ds6_addr_add(&ipaddr, 0, ADDR_AUTOCONF);
}

/*-----------------------------------------------------------
 * BORDER ROUTER PROCESS
 *-----------------------------------------------------------
 */ 

PROCESS_THREAD(border_router_process, ev, data)
{
  static struct etimer et;
  rpl_dag_t *dag;
  PROCESS_BEGIN();

  /* While waiting for the prefix to be sent through the SLIP connection, the future
   * border router can join an existing DAG as a parent or child, or acquire a default 
   * router that will later take precedence over the SLIP fallback interface.
   * Prevent that by turning the radio off until we are initialized as a DAG root.
   */
  prefix_set = 0;
  NETSTACK_MAC.off(0);
  PROCESS_PAUSE();
  SENSORS_ACTIVATE(button_sensor);

  printf("RPL-Border router started\n");

  /* Request prefix until it has been received */
  while(!prefix_set) {
    etimer_set(&et, CLOCK_SECOND);
    request_prefix();
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
  }

  dag = rpl_set_root(RPL_DEFAULT_INSTANCE,(uip_ip6addr_t *)dag_id);
  if(dag != NULL) {
    rpl_set_prefix(dag, &prefix, 64);
    printf("created a new RPL dag\n");
  }

  /* Now turn the radio on, but disable radio duty cycling.
   * Since we are the DAG root, reception delays would constrain mesh throughbut.
   */
  NETSTACK_MAC.off(1);
  leds_init();
  
  while(1) {
    PROCESS_YIELD();

    if (ev == sensors_event && data == &button_sensor) {
      PRINTF("Initiating global repair\n");
      rpl_repair_root(RPL_DEFAULT_INSTANCE);
    }
  }
  PROCESS_END();
}



/*-----------------------------------------------------------
 * WEB SERVER PROCESS
 *-----------------------------------------------------------
 */ 

/* Use simple webserver with only one page for minimum footprint.
 * Multiple connections can result in interleaved tcp segments since
 * a single static buffer is used for all segments.
 * The internal webserver can provide additional information if
 * enough program flash is available.
 */
#define WEBSERVER_CONF_LOADTIME 0
#define WEBSERVER_CONF_FILESTATS 0
#define WEBSERVER_CONF_NEIGHBOR_STATUS 0
#define WEBSERVER_CONF_ROUTE_LINKS 0
#if WEBSERVER_CONF_ROUTE_LINKS
#define BUF_USES_STACK 1
#endif

static const char *TOP = "<html><head><title>ContikiRPL</title></head><body>\n";
static const char *BOTTOM = "</body></html>\n";
#if BUF_USES_STACK
static char *bufptr, *bufend;

#define ADD(...) do {                                                   \
    bufptr += snprintf(bufptr, bufend - bufptr, __VA_ARGS__);      \
  } while(0)
#else
static char http_buf[256];
static int blen;
#define ADD(...) do {                                                   \
    blen += snprintf(&http_buf[blen], sizeof(http_buf) - blen, __VA_ARGS__);      \
  } while(0)
#endif

static void
ipaddr_add(const uip_ipaddr_t *addr)
{
  uint16_t a;
  int i, f;
  for(i = 0, f = 0; i < sizeof(uip_ipaddr_t); i += 2) {
    a = (addr->u8[i] << 8) + addr->u8[i + 1];
    if(a == 0 && f >= 0) {
      if(f++ == 0) ADD("::");
    } else {
      if(f > 0) {
        f = -1;
      } else if(i > 0) {
        ADD(":");
      }
      ADD("%x", a);
    }
  }
}

static
PT_THREAD(generate_routes(struct httpd_state *s))
{
  //static int i;
  static uip_ds6_route_t *r;
  static uip_ds6_nbr_t *nbr;
#if BUF_USES_STACK
  char http_buf[256];
#endif
#if WEBSERVER_CONF_LOADTIME
  static clock_time_t numticks;
  numticks = clock_time();
#endif

  PSOCK_BEGIN(&s->sout);

  SEND_STRING(&s->sout, TOP);
#if BUF_USES_STACK
  bufptr = http_buf;bufend=bufptr+sizeof(http_buf);
#else
  blen = 0;
#endif
  ADD("Neighbors<pre>");

  for(nbr = nbr_table_head(ds6_neighbors);
      nbr != NULL;
      nbr = nbr_table_next(ds6_neighbors, nbr)) {

#if WEBSERVER_CONF_NEIGHBOR_STATUS
#if BUF_USES_STACK
{char* j=bufptr+25;
      ipaddr_add(&nbr->ipaddr);
      while (bufptr < j) ADD(" ");
      switch (nbr->state) {
      case NBR_INCOMPLETE: ADD(" INCOMPLETE");break;
      case NBR_REACHABLE: ADD(" REACHABLE");break;
      case NBR_STALE: ADD(" STALE");break;      
      case NBR_DELAY: ADD(" DELAY");break;
      case NBR_PROBE: ADD(" NBR_PROBE");break;
      }
}
#else
{uint8_t j=blen+25;
      ipaddr_add(&nbr->ipaddr);
      while (blen < j) ADD(" ");
      switch (nbr->state) {
      case NBR_INCOMPLETE: ADD(" INCOMPLETE");break;
      case NBR_REACHABLE: ADD(" REACHABLE");break;
      case NBR_STALE: ADD(" STALE");break;      
      case NBR_DELAY: ADD(" DELAY");break;
      case NBR_PROBE: ADD(" NBR_PROBE");break;
      }
}
#endif
#else
      ipaddr_add(&nbr->ipaddr);
#endif

      ADD("\n");
#if BUF_USES_STACK
      if(bufptr > bufend - 45) {
        SEND_STRING(&s->sout, http_buf);
        bufptr = http_buf; bufend = bufptr + sizeof(http_buf);
      }
#else
      if(blen > sizeof(http_buf) - 45) {
        SEND_STRING(&s->sout, http_buf);
        blen = 0;
      }
#endif
  }

  ADD("</pre>Probe status<pre>");

  if(is_probe_connected==false){
    ADD("Probe 1 : Not connected\n");
  } else {
    ADD("Probe 1 : Connected \n");
  } 
  
  ADD("</pre>System status<pre>");
  if(in_emergency_mode==false){
    ADD("Emergency system is OK\n");
  } else{
    ADD("Emergency system has been ACTIVATED!\n");
  }

  ADD("</pre>Routes<pre>");
  SEND_STRING(&s->sout, http_buf);
#if BUF_USES_STACK
  bufptr = http_buf; bufend = bufptr + sizeof(http_buf);
#else
  blen = 0;
#endif

  for(r = uip_ds6_route_head(); r != NULL; r = uip_ds6_route_next(r)) {

#if BUF_USES_STACK
#if WEBSERVER_CONF_ROUTE_LINKS
    ADD("<a href=http://[");
    ipaddr_add(&r->ipaddr);
    ADD("]/status.shtml>");
    ipaddr_add(&r->ipaddr);
    ADD("</a>");
#else
    ipaddr_add(&r->ipaddr);
#endif
#else
#if WEBSERVER_CONF_ROUTE_LINKS
    ADD("<a href=http://[");
    ipaddr_add(&r->ipaddr);
    ADD("]/status.shtml>");
    SEND_STRING(&s->sout, http_buf); //TODO: why tunslip6 needs an output here, wpcapslip does not
    blen = 0;
    ipaddr_add(&r->ipaddr);
    ADD("</a>");
#else
    ipaddr_add(&r->ipaddr);
#endif
#endif
    ADD("/%u (via ", r->length);
    ipaddr_add(uip_ds6_route_nexthop(r));
    if(1 || (r->state.lifetime < 600)) {
      ADD(") %lus\n", r->state.lifetime);
    } else {
      ADD(")\n");
    }
    SEND_STRING(&s->sout, http_buf);
#if BUF_USES_STACK
    bufptr = http_buf; bufend = bufptr + sizeof(http_buf);
#else
    blen = 0;
#endif
  }
  ADD("</pre>");

#if WEBSERVER_CONF_FILESTATS
  static uint16_t numtimes;
  ADD("<br><i>This page sent %u times</i>",++numtimes);
#endif

#if WEBSERVER_CONF_LOADTIME
  numticks = clock_time() - numticks + 1;
  ADD(" <i>(%u.%02u sec)</i>",numticks/CLOCK_SECOND,(100*(numticks%CLOCK_SECOND))/CLOCK_SECOND));
#endif

  SEND_STRING(&s->sout, http_buf);
  SEND_STRING(&s->sout, BOTTOM);

  PSOCK_END(&s->sout);
}
/*---------------------------------------------------------------------------*/
httpd_simple_script_t
httpd_simple_get_script(const char *name)
{
  return generate_routes;
}

PROCESS_THREAD(webserver_nogui_process, ev, data)
{
  PROCESS_BEGIN();

  httpd_init();

  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(ev == tcpip_event);
    httpd_appcall(data);
  }
  
  PROCESS_END();
}

