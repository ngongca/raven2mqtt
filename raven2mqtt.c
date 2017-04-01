/* =====================================================================================================*/
/* RAVEN2MQTT.C - Read a USB Com port and publish demand data to MQTT Broker				*/
/*													*/
/* Author: Nick Ong 03/28/2017                                                                          */
/*         Started with Andy Wysocki's code  03/16/2013							*/
/*													*/
/* This code is release under the Open Software License v. 3.0 (OSL-3.0)				*/
/*					http://opensource.org/licenses/OSL-3.0			        */
/*											           	*/
/*	Release 1.0 - 03/28/2018							      		*/
/*													*/
/* ==================================================================================================== */

#include <stdarg.h>
#include <stdlib.h>
#include <getopt.h>
#include "parse.h"
#include "dbglog.h"


#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <utime.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include "MQTTClient.h"

enum {
  SUCCESSFUL = 0,
  ERR_BADFDOPEN = -1,
  ERR_BADFHOPEN= -2,
  ERR_BADPARM = -3,
};

char	*gStartUp = "\n\nraven2mqtt - \nVersion 0.1 Mar 28, 2017\nRead COM port for Rainforest RAVEn device and publish data to mqtt\r\n\r\n";

char    *gApp = "Raven2mqtt";
char    *gINIFile="raven2mqtt.ini";

char	gDBGVerbose = 0;
char    gDebugLog[255];
char    gDebug;
char	gDbgBuf[2048];


char	gReadBuf[1024];
char	gXMLBuffer[10*1024];
int	gXMLMaxBufferLen = sizeof( gXMLBuffer );
int	gXMLBufferLen;

char	gUSBDev[128];

int	gFD;
FILE	*gFH;

char	gCmdBuffer[1024*5];
int	gCmdBufferLen;

#define READ_BINARY	"rb"
#define WRITE_BINARY 	"wb"

#define QOS         	1
#define TIMEOUT     	10000L

MQTTClient gMQTTClient;
char gMQTTBrokerAddress[128];
char gMQTTClientID[128];
char gMQTTPayload[1024];
char gMQTTTopic[256];

//----------------------------------------
//----------------------------------------
void LoadINIParms( );
void ProcessData ( void );
int OpenPort( void );
int ConfigurePort( int );
int FormatCmdBuffer( char *_cmd );


//----------------------------------------
//----------------------------------------
int main (argc, argv, envp)
     int argc;
     char *argv[];
     char *envp;
{
  int	c;
  int	rc;
  char	buf[1024];

  while( ( c = getopt( argc, argv, "v?h" ) ) != -1 )
    {
      switch( c )
	{
	case 'v':
	  gDBGVerbose = 1;
	  break;
	case '?':
	case 'h':
	  printf( gStartUp );
	  printf( "\r\n-v - VERBOSE print everything that would go to DEBUG LOG if debug was turned on\r\n" );
	  exit(SUCCESSFUL);
	  break;
	default:
	  printf("? Unrecognizable switch [%s] - program aborted\n", optarg );
	  exit( ERR_BADPARM );
	}
    }
  LoadINIParms();               // Get my custom parms
  InitDBGLog( "RAVEn", gDebugLog, gDebug, gDBGVerbose);
  WriteDBGLog( gStartUp );
  rc = OpenPort();
  if ( !rc )
    {
      ProcessData();
    }
  return rc;

}

/*===========================================================================*/
/* SENDCMD - Send a RAVEn command to the device								 */
/*===========================================================================*/
int SendCmd( char *_cmd )
{
  int	len;

  if ( strlen( _cmd ) > 1024 )
    return ( -1);
  memset( gCmdBuffer, 0, sizeof( gCmdBuffer) );
  gCmdBufferLen = sprintf( gCmdBuffer, "<Command>\r\n  <Name>%s</Name>\r\n</Command>\r\n", _cmd );
  WriteDBGLog( gCmdBuffer );
  len = write( gFD, gCmdBuffer, gCmdBufferLen );
  if ( len != gCmdBufferLen )
    {
      sprintf( gDbgBuf, "Error writing Command to RAVEn port, %d != %d", len, gCmdBufferLen );
      WriteDBGLog( gDbgBuf );
      return( -1 );
    }
  return( 0 );
}

/*===========================================================================*/
/* LOADINIPARMS - Load in the INI file                                       */
/*===========================================================================*/
void LoadINIParms( )
{
  char locbuf[256];

  int		rc = 0;
  /* Get the GLOBAL Section */
  GetIniString( gApp, "DebugLog", "./raven2db.log", gDebugLog, sizeof( gDebugLog ), gINIFile );
  GetIniString( gApp,   "Debug", "No", locbuf, sizeof( locbuf ), gINIFile );
  gDebug = CheckYes( locbuf );
  GetIniString( gApp, "USBDev", "/dev/ttyUSB0", gUSBDev, sizeof( gUSBDev ), gINIFile );
  GetIniString( gApp, "MQTTBrokerAddress", "http://localhost", gMQTTBrokerAddress, sizeof( gMQTTBrokerAddress ), gINIFile );
  GetIniString( gApp, "MQTTClientID", "clientID", gMQTTClientID, sizeof( gMQTTClientID ), gINIFile );


}

/*===========================================================================*/
/* OPENPORT - Open the USB Port for reading and writing						 */
/*===========================================================================*/
int OpenPort(void)
{
  int fd; // file description for the serial port
	
  sprintf( gDbgBuf, "Opening port [%s]", gUSBDev );
  WriteDBGLog( gDbgBuf );
  gFD = open(gUSBDev, O_RDWR);
  if(gFD == -1) // if open is unsucessful
    {
      sprintf(gDbgBuf, "open_port: Unable to open %s. 0x%0x - %s\n", gUSBDev, errno, strerror( errno ) );
      WriteDBGLog( gDbgBuf );
      perror( gDbgBuf );
      return( ERR_BADFDOPEN );
    }
  else
    {
      gFH = fdopen( gFD, "r" );
      if ( gFH == NULL )
	{
	  sprintf(gDbgBuf, "open_port: Unable to open %s. 0x%0x - %s\n", gUSBDev, errno, strerror( errno ) );
	  WriteDBGLog( gDbgBuf );
	  perror( gDbgBuf );
	  return( ERR_BADFHOPEN );

	}
      WriteDBGLog( "Port is open, both File Descriptor and File Handle" );
    }
  return(SUCCESSFUL);
} //open_port

/*===========================================================================*/
/* MQTTClient_init - initalize MQTT client                                   */
/*===========================================================================*/
int MQTTClient_init()
{
  int rc;
  MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
  
  MQTTClient_create(&gMQTTClient, gMQTTBrokerAddress, gMQTTClientID, MQTTCLIENT_PERSISTENCE_NONE, NULL);
  conn_opts.keepAliveInterval = 20;
  conn_opts.cleansession = 1;

  if ((rc = MQTTClient_connect(gMQTTClient, &conn_opts)) != MQTTCLIENT_SUCCESS)
    {
      sprintf(gDbgBuf,"Failed to connect, return code %d\n", rc);
      WriteDBGLog( gDbgBuf );
      exit(EXIT_FAILURE);
    }
  return rc;
}  


/*===========================================================================*/
/* ParseRavenXML - parse XML in buffer and set value and topic               */
/*   returns true if a match for instant demand tag.
/* assumes that the buffer contains only one tag with elements               */
/*===========================================================================*/
int ParseRavenXML(char* buffer)
{
  char* p;
  int demand;
  uint demand_u;
  float demand_result;
  char* macid;
  uint multiplier;
  uint divisor;
  uint timestamp;

  p = strtok( buffer,"<>\n" );
  if ( strcmp(p, "InstantaneousDemand") == 0 )
    {
      p = strtok( NULL, "<>\n" );
      while ( p != NULL )
	{
	  if ( strcmp(p, "Demand") == 0 )
	    {
	      sscanf( strtok( NULL, "<>\n" ), "0x%x", &demand_u );
	      demand = demand_u;
	      if ( demand >= 2^23 ) demand = demand - 2^24;
	    }
	  else if ( strcmp(p, "DeviceMacId") == 0 )
	    {
	      macid = strtok( NULL, "<>\n" );	      
	    }
	  else if ( strcmp(p, "Multiplier" ) == 0 )
	    {
	      sscanf( strtok( NULL, "<>\n" ), "0x%x", &multiplier );
	      if ( multiplier == 0 ) multiplier = 1;
	    }
	  else if ( strcmp(p, "Divisor" ) == 0 )
	    {
	      sscanf( strtok( NULL, "<>\n" ), "0x%x", &divisor );
	      if ( divisor == 0 ) divisor = 1;
	    }
	  else if ( strcmp(p, "TimeStamp" ) == 0 )
	    {
	      sscanf( strtok( NULL, "<>\n" ), "0x%x", &timestamp );
	      timestamp = timestamp + 946684806;
	    }
	  p = strtok( NULL, "<>\n" );
	}
      demand_result = (double) demand * (double) multiplier / (double) divisor;
      sprintf( gMQTTPayload,"{\"demand\":{\"timestamp\":%d,\"value\":%09.3f}}", timestamp, demand_result );
      sprintf( gMQTTTopic, "home/%s/demand", macid ); 
      return 1;
    }
  else
    {
      return 0;
    }
}
  

/*===========================================================================*/
/* PROCESSDATA - Process the usb com port and store XML data into SQL DB     */
/*===========================================================================*/
void ProcessData(  )
{

  MQTTClient_message pubmsg = MQTTClient_message_initializer;
  MQTTClient_deliveryToken token;
  int rc;
  int rblen;
  int n, len;
  fd_set rdfs;
  struct timeval timeout;
  char l[800];
  char locbuf[1024];
  int f;
  int i;
  int loop = 1;
  int first = 0;

  SendCmd("initialize"); 
  WriteDBGLog( "Starting to PROCESS input" );
  memset( gReadBuf, 0, sizeof( gReadBuf ) );
  gXMLBufferLen = 0;
  while( ( fgets( gReadBuf, sizeof( gReadBuf ), gFH ) ) > 0 )
    {
      rblen = strlen( gReadBuf );
      /* If Current buffer size + new Buffer being added is over the total buffer size BAD overflow */
      if ( (gXMLBufferLen + rblen) > gXMLMaxBufferLen )
	{
	  WriteDBGLog( "Error BUFFER OVERFLOW" );
	  WriteDBGLog( gXMLBuffer );
	  WriteDBGLog( gReadBuf );
	}
      else
	{
	  strcat( gXMLBuffer, gReadBuf );
	  gXMLBufferLen += rblen;
	  /* Check if this is the final XML tag Ending */
	  /* Since I'm not really parsing XML, I am assuming the Rainforest dongle is spitting out its specific XML */
	  /* They always add two space for XML inbetween the start and stop So the closing XML will always be </    */
	  if ( strncmp( gReadBuf, "</",  2 ) == 0 )
	    {
	      if ( ParseRavenXML( gXMLBuffer ) )
		{
		  sprintf(gDbgBuf, "Attempting to create client at address %s ClientID %s", gMQTTBrokerAddress, gMQTTClientID); 
		  WriteDBGLog(gDbgBuf);
		  MQTTClient_init();
		  pubmsg.payload = gMQTTPayload;
		  pubmsg.payloadlen = strlen(gMQTTPayload);
		  pubmsg.qos = QOS;
		  pubmsg.retained = 0;
		  sprintf(gDbgBuf, "publishing - %s",gXMLBuffer);
		  WriteDBGLog(gDbgBuf);
		  MQTTClient_publishMessage(gMQTTClient, gMQTTTopic, &pubmsg, &token);
		  sprintf(gDbgBuf, "Waiting for up to %d seconds for publication of \n%s\non topic %s for client with ClientID: %s\n",(int)(TIMEOUT/1000), gMQTTPayload ,gMQTTTopic, gMQTTClientID);
		  WriteDBGLog( gDbgBuf );
		  rc = MQTTClient_waitForCompletion(gMQTTClient, token, TIMEOUT);
		  sprintf(gDbgBuf, "Message with delivery token %d delivered\n", token);
		  WriteDBGLog( gDbgBuf );
		  MQTTClient_disconnect(gMQTTClient, 10000);
		  MQTTClient_destroy(&gMQTTClient);
		}

	      // Write to the LOG file if its turned on
	      WriteDBGLog( gXMLBuffer );
	      // Clear the XML buffer
	      memset( gXMLBuffer, 0, sizeof( gXMLBuffer ) );
	      gXMLBufferLen = 0;
	    }
	}
      memset( gReadBuf, 0, sizeof( gReadBuf ) );
    }
  return ;
}
