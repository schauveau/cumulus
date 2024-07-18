#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>
#include <string.h>
#include <inttypes.h>

#ifdef ESP_PLATFORM

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#else

#include <linux/tcp.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdbool.h>

#endif

#include "telnet.h"

 
#ifndef DEBUG_TELNET
//  0 : telnet server is silent
//  1 : telnet server print a few message
//  2 : telnet server is verbose
#define DEBUG_TELNET 1
#endif

// TODO: Process the TELNET COMMANDS (IAC ...) 
#define TC_IAC     255             /* interpret as command: */
#define TC_DONT    254             /* you are not to use option */
#define TC_DO      253             /* please, you use option */
#define TC_WONT    252             /* I won't use option */
#define TC_WILL    251             /* I will use option */
#define TC_SB      250             /* interpret as subnegotiation */
#define TC_GA      249             /* you may reverse the line */
#define TC_EL      248             /* erase the current line */
#define TC_EC      247             /* erase the current character */
#define TC_AYT     246             /* are you there */
#define TC_AO      245             /* abort output--but let prog finish */
#define TC_IP      244             /* interrupt process--permanently */
#define TC_BREAK   243             /* break */
#define TC_DM      242             /* data mark--for connect. cleaning */
#define TC_NOP     241             /* nop */
#define TC_SE      240             /* end sub negotiation */
#define TC_EOR     239             /* end of record (transparent mode) */
#define TC_ABORT   238             /* Abort process */
#define TC_SUSP    237             /* Suspend process */
#define TC_xEOF    236             /* End of file: EOF is already used... */
#define TC_SYNCH   242             /* for telfunc calls */


#define KEEPALIVE_IDLE 5
#define KEEPALIVE_INTERVAL 5
#define KEEPALIVE_COUNT 3

#define TELNET_MAX_COMMAND_COUNT 8 

#define CRLF "\r\n"

static const telnet_command_t *
find_command(telnet_server_t *server, const char *name) {
  int k; 
  for (k=0; k<server->command_count; k++) {
    if ( !strcmp(name,server->commands[k].name) ) {
      return & server->commands[k] ;
    }
  }
  return NULL ;
}

// Send a raw buffer to the telnet client
//
// Return the number of character sent or a negative a value if a problem occured. 
// 
// If len is negative then nothing is sent and 0 is returned. 
//
int telnet_send_raw(telnet_server_t *server, const char *data, int len) {
  if (len<=0)
    return 0; 
  int remain = len; 
  while (remain>0) {
    int n = send(server->sock, data, remain, 0);
    if (n<0) {
      printf( "Error occurred during sending: errno %d\n", errno);
      return -1;
    }
    data += n;
    remain -= n;
  }
  return len; 
}

int
telnet_send_buffer(telnet_server_t *server, const char *data, int len)
{
  int count=0 ; // total number of characters sent
  int start=0 ;
  for (int i=0; i<=len; i++) {
    
    if ( i==len || data[i]=='\0' || data[i]=='\n' ) {
      // Send data[start:i-1] 
      int n = telnet_send_raw(server, data+start, i-start );
      if (n<0)
        return n;
      count+=n ;
      // and skip current character
      start=i+1; 
    }

    // Replace skipped '\n' by CRLF 
    if ( i<len && data[i]=='\n' ) {
      int n = telnet_send_raw(server, "\r\n", 2);
      if (n<0)
        return n;
      count+=n ;
    }
  }
  return count; 
}

int telnet_send_string(telnet_server_t *server, const char *str)
{
  return telnet_send_buffer(server, str, strlen(str)); 
}

int telnet_printf(telnet_server_t *server, const char *format, ...)
{
  char buffer[256];
  char *buffer2=NULL;
  va_list args;
  int len, len2;
  int res = 0;
  
  va_start(args, format);
  len = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
   
  if (len<0) {
    res=-1;
    goto done; 
  }

  if (len==0) {
    // No output. 
    res=0;
    goto done;
  }

  if (len < sizeof(buffer)) {
    res = telnet_send_buffer(server, buffer, len);
    goto done; 
  }

  // The static buffer is too small. Try again
  buffer2 = malloc(len+1);
  if (buffer2==NULL) {
    res=-2 ;
    goto done; 
  }

  va_start(args, format);
  len2 = vsnprintf(buffer2, len+1, format, args);
  va_end(args);
  
  if (len<0 || len2 >= len+1) {
    res=-3 ;
    goto done; 
  }

  res = telnet_send_buffer(server, buffer, len);
  
  done:
  
  if (buffer2)
    free(buffer2);
  return res; 
}

void telnet_will(telnet_server_t *server, char option) {
  char msg[3] = { TC_IAC, TC_WILL, option};
  telnet_send_raw(server, msg, 3);
}

void telnet_wont(telnet_server_t *server, char option) {
  char msg[3] = { TC_IAC, TC_WONT, option};
  telnet_send_raw(server, msg, 3);
}

void telnet_do(telnet_server_t *server, char option) {
  char msg[3] = { TC_IAC, TC_DO, option};
  telnet_send_raw(server, msg, 3);
}


void telnet_dont(telnet_server_t *server, char option) {
  char msg[3] = { TC_IAC, TC_DONT, option};
  telnet_send_raw(server, msg, 3);
}
  
void telnet_cmd_not_implemented(telnet_server_t *server, const telnet_command_t *cmd, int narg, const telnet_argval_t *arg)
{
  telnet_send_string(server, "Error: not implemented\n");
}

void telnet_cmd_quit(telnet_server_t *server, const telnet_command_t *cmd, int narg, const telnet_argval_t *arg)
{
  server->connected = false; 
  telnet_send_string(server, "bye\n");
}

void telnet_cmd_help(telnet_server_t *server, const telnet_command_t *telnet_cmd, int narg, const telnet_argval_t *arg)
{
  if (narg==0) {
    const telnet_command_t *cmd = server->commands;
    for (int i=0 ; i<server->command_count ; i++, cmd++) {
      const char * short_help =  cmd->help[0] ? cmd->help[0] : "Undocumented" ;
      const char * arg_help =  cmd->help[1] ? cmd->help[1] : "" ;
      telnet_send_string(server, cmd->name );
      telnet_send_string(server, " " );
      telnet_send_string(server, arg_help );
      telnet_send_string(server, "\n     " );
      telnet_send_string(server, short_help );
      telnet_send_string(server, "\n");
    }
    return;
  }

  const char *name = (arg++)->s ;
  
  const telnet_command_t *cmd = find_command(server, name);
  
  if (cmd==NULL) {
    telnet_send_string(server, "Error: Unknown command '");
    telnet_send_string(server, name);
    telnet_send_string(server, "'");
    return;
  }
    
  const char * short_help =  cmd->help[0] ? cmd->help[0] : "Undocumented" ;
  const char * arg_help =  cmd->help[1] ? cmd->help[1] : "" ;

  telnet_printf(server, "\nUsage: %s %s\n\n",cmd->name,arg_help) ;

  telnet_send_string(server, short_help) ;
  telnet_send_string(server, "\n\n") ;
  
  if (cmd->help[2]) {
    telnet_send_string(server, cmd->help[2]) ;
    telnet_send_string(server, "\n") ;
  }
}


// Mark the end of the word starting at ptr by a '\0'
// and return the next character. 
static char *skip_word(char *at) {
  while (1) {
    char c = *at ;
    if (  c==' ' || c=='\t' ) {
      *at = '\0'; 
      return at+1;
    } else if (c=='\0') {
      return at; 
    }
    at++ ;
  }
}

static char * skip_blanks(char *ptr) {
  while ( *ptr==' ' || *ptr=='\t' )
    ptr++;
  return ptr; 
}



static void parse_command(telnet_server_t *server, char *command)
{
  telnet_argval_t arg[8] ;
  char *at = skip_blanks(command);
    
  if (*at=='\0') {
    printf("Empty command"CRLF);
    return;  
  }
  const char *cmd_name = at;
  at = skip_word(at); 
  
  const telnet_command_t *cmd = find_command(server, cmd_name);      
  if (!cmd) {
    telnet_send_string(server,"[Error] Unknown command\n");
    return;
  }
  
  int narg=0 ;
  int ok=1;
  int optargs=0;
  for ( const char *args = cmd->args; *args ; args++ ) {
    char c=*args;
    // printf(" -> '%s'\n",at);
    
    // ? marks the start of the optional arguments
    if (c=='?') {
      optargs=1;
      continue; 
    }
    
    at=skip_blanks(at);
    
    if (c=='r') {
      // Everything remaining is treated as the last argument
      arg[narg++].s = at;
      at += strlen(at) ;
      continue;
    }
        
    if (*at=='\0') {
      if (!optargs) {
        telnet_send_string(server,"Error: missing argument\n");
        ok=0;
      }
      break;
    }
    
    char *start = at;
    at = skip_word(at); 
    
    if (c=='w') {
      // Argument is the next word (no blanks)
      //printf("got word '%s'\n",start);
      arg[narg++].s = start;
    } else if (c=='i') {
      // Argument is integer         
      int value;
      char dummy;
      if (sscanf(start,"%d%c",&value,&dummy)!=1) {
        telnet_send_string(server,"Error: bad argument. Integer value expected\n");
        break;
      }
      printf("#%d is integer %d\n",narg, value);
      arg[narg++].i = value;  
      continue;            
    } else if (c=='f') {
      // Argument is floating-point
      float value;
      char dummy;
      if (sscanf(start,"%f%c",&value,&dummy)!=1) {
        telnet_send_string(server,"Error: bad argument. Floating point value expected\n");
        break;
      }
      printf("#%d is float %f\n",narg, value);
      arg[narg++].f = value;            
      continue;
    }      
  }
      
  if (ok) {
    telnet_callback_t callback = cmd->callback ;
    if (!callback) {
      callback = &telnet_cmd_not_implemented; 
    }      
    callback(server, cmd, narg, arg) ;
  }
      
  telnet_send_string(server,"\n");
}



static void process_connection(telnet_server_t *server)
{
  enum { BUFSIZE=128 } ;
  
  enum {
    BFS_DEFAULT,
    BFS_IAC,          // after IAC
    BFS_IAC_DO,       // after IAC DO
    BFS_IAC_DONT,     // after IAC DONT
    BFS_IAC_WILL,     // after IAC WILL
    BFS_IAC_WONT,     // after IAC WONT    
  } state = BFS_DEFAULT; 

  char buffer[BUFSIZE];
  const char *prompt="# " ;

  telnet_send_string(server,
                     "Welcome to Cumulus\n"
                     "Use 'help' to get a list of commands\n" 
                     "Use 'quit' or CTRL-C to close the connection\n"
                     "\n"   
    );

  bool truncated=false ;
  int pending=0 ; // the amount of data that is still to be processed in buffer[0..pending-1] 
  state = BFS_DEFAULT;
  bool need_prompt=true;
  while(server->connected) {

    //////////////////////////////// 
    // Receive and process more data
    //////////////////////////////// 

    if (pending==sizeof(buffer)-1) {
      // The buffer is full. 
      pending=0;
      truncated=true; 
    }
    
    if (need_prompt) {
      telnet_send_string(server,prompt);
      need_prompt = false; 
    }
    
    int nb = recv(server->sock, buffer+pending, sizeof(buffer)-pending-1, 0);

    if (DEBUG_TELNET>1)
      printf( "Received %d chars\n",nb);

    if (nb < 0) {
      if (DEBUG_TELNET) printf( "Error: communication error (errno=%d)\n", errno);
      return ; 
    }
    
    if (nb == 0) {
      if (DEBUG_TELNET) printf( "Connection closed\n");
      break ;
    }
    
    // Regular non-control characters will be appended at 'end' 
    char *end = buffer+pending;
    bool interrupted = false; // CTRL-C detection 
    for (int i=0;i<nb;i++) {
      // Warning: Each iteration should not append more than once to `end`
      unsigned char c = buffer[pending+i];
      switch(state) {
        case BFS_DEFAULT:
          if (c==TC_IAC) {
            state=BFS_IAC;
          } else if (c==0) {
            // ignore \0 bytes
          } else if (c==4) {
            // This is EOT (End-of-transmission) or CTRL-D
            interrupted=true;
            i=nb; 
          } else if (c=='\r') {
            // ignore \r so we dont have to care about CRLF 
          } else {
            // store for later processing.
            *end++ = c;            
          }
          break;
        case BFS_IAC:
          switch (c) {
            case TC_IAC:
              *end++ = TC_IAC;
              break;
            case TC_DO:
              state=BFS_IAC_DO ;
              break; 
            case TC_DONT:
              state=BFS_IAC_DONT ;
              break; 
            case TC_WILL:
              state=BFS_IAC_WILL ;
              break; 
            case TC_WONT:
              state=BFS_IAC_WONT ;
              break;
            case TC_IP:  
              // This is CTRL-C
              interrupted=true;
              i=nb ;
              break;
            default:
              // for now, ignore all other commands
              printf("IAC %d\n", c&0xFF);
              state = BFS_DEFAULT; 
              break;
          }
          break;
        case BFS_IAC_DO:
          // We wont be doing 'c'
          telnet_wont(server,c);
          state = BFS_DEFAULT; 
          break; 
        case BFS_IAC_WILL:
          // We do not want the client to do 'c'  
          telnet_dont(server,c);
          state = BFS_DEFAULT; 
          break; 
        case BFS_IAC_DONT:
        case BFS_IAC_WONT:
          // For now, ignore those commands
          state = BFS_DEFAULT; 
          break ;
      }
    }

    *end ='\0'; 

    if (DEBUG_TELNET>1) {
      printf("::: %d [%s]\n::: ",strlen(buffer), buffer) ;

      for (char *ptr=buffer ; ptr!=end ; ptr++) {
        printf("%02X ",(unsigned char) *ptr) ;
      }
      printf("\n");
    }
    
    char *pos = buffer;
    while (server->connected) {
      char *line = pos;
      char *end_of_line = strchr(pos,'\n');      
      if (end_of_line==NULL)
        break;
      *end_of_line = '\0';
      need_prompt = true; 
      if (truncated) {
        telnet_send_string(server,"[Error] line too long\n");
        truncated=false;
      } else {
        parse_command(server,line);
      }
      pos = end_of_line+1 ;
    }

    if (interrupted)
      server->connected = false ;
    
    // Move whatever was not yet processed to the beginning of the buffer
    pending = strlen(pos);
    if (pending>0 && pos!=buffer) { 
      memmove(buffer,pos,pending+1); 
    }

    
  } ;
  
}
    

static void telnet_task(void *data) {

  telnet_server_t *server = (telnet_server_t *)data;
      
  char *addr_str;
  //char addr_str[128];
  int addr_family = AF_INET ;  // IPV4
  int ip_protocol = 0;
  int keepAlive = 1;
  int keepIdle = KEEPALIVE_IDLE;
  int keepInterval = KEEPALIVE_INTERVAL;
  int keepCount = KEEPALIVE_COUNT;
  struct sockaddr_storage dest_addr;

  if (addr_family == AF_INET) {
    struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
    dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr_ip4->sin_family = AF_INET;
    dest_addr_ip4->sin_port = htons(server->port);
    ip_protocol = IPPROTO_IP;
  }

  int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
  if (listen_sock < 0) {
    if (DEBUG_TELNET) printf("Unable to create socket: errno %d\n", errno);
    return;
  }
  int opt = 1;
  setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  if (DEBUG_TELNET) printf("Socket created\n");
  
  int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
  if (err != 0) {
    if (DEBUG_TELNET) printf( "Socket unable to bind: errno %d\n", errno);
    if (DEBUG_TELNET) printf( "IPPROTO: %d\n", addr_family);
    goto CLEAN_UP;
  }
  if (DEBUG_TELNET) printf( "Socket bound, port %d\n", server->port);
    
  err = listen(listen_sock, 1);
  if (err != 0) {
    if (DEBUG_TELNET) printf( "Error occurred during listen: errno %d\n", errno);
    goto CLEAN_UP;
  }

  while (1) {

    struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
    socklen_t addr_len = sizeof(source_addr);
      
    if (DEBUG_TELNET) printf( "Socket listening\n");

    int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
    if (sock < 0) {
      if (DEBUG_TELNET) printf( "Unable to accept connection: errno %d\n", errno);
      break;
    }

    // Set tcp keepalive option
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));

    if (DEBUG_TELNET) {
      // Convert ip address as string
      
       if (source_addr.ss_family == PF_INET) {
        //inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        addr_str = inet_ntoa(((struct sockaddr_in *)&source_addr)->sin_addr);
        printf("Socket accepted ip address: %s\n", addr_str);
      }
    }

    server->sock = sock ;
    server->connected=true;
    process_connection(server);
    server->connected=false;

    shutdown(sock, 0);
    close(sock);
  }

  CLEAN_UP:
  close(listen_sock);

}

void telnet_start_server(telnet_server_t *server)
{
  if (server->port<=0)
    server->port = TELNET_DEFAULT_PORT; 

  server->connected = false;
  server->sock = -1;
    
#ifdef ESP_PLATFORM
  xTaskCreate(telnet_task, "telnet", 4096, server, 10, NULL);
#else
  telnet_task(server);
#endif
}


///////////////////////////////////////////////////////////////////////////////

#ifdef TELNET_MAIN

// Build a simple app for testing 

void telnet_cmd_restart(telnet_server_t *server, const telnet_command_t *cmd, int narg, const telnet_argval_t *arg) {
}

void telnet_cmd_info(telnet_server_t *server, const telnet_command_t *cmd, int narg, const telnet_argval_t *arg) {
}

void telnet_cmd_ccc(telnet_server_t *server, const telnet_command_t *cmd, int narg, const telnet_argval_t *arg) {
  char buffer[50];
  if (narg==2) {
    int   ival = (arg++)->i ;
    float fval = (arg++)->f ;
    //sprintf(buffer,"==> %i / %f\n", ival, fval);
    //telnet_send_string(server, buffer);
    //sprintf(buffer,"==> %i / %f\n", ival, fval);
    telnet_printf(server, "=aa=> %i / %f\n", ival, fval);
  }
}

telnet_command_t Commands[] = {
  TELNET_DEFAULT_COMMANDS,
  { .name="restart" ,
    .args="",
    .help = {
      "Restart the device"
    },
  }, 
  {
    .name="info" ,
    .args="i",
    .help={
    }
  }, 
  {
    .name="bbb" ,
    .args="f",
    .help={
    }
  }, 
  {
    .name="ccc" ,
    .args="if",
    .callback = &telnet_cmd_ccc,
    .help={
    }
  }, 
  {
    .name="ccc" ,
    .args="if",
    .callback = &telnet_cmd_ccc,
    .help={
    }
  }, 
};

telnet_server_t server = {
  .port = 5555,  // reminder: 23 is the standard telnet port
  .commands = Commands,
  .command_count = sizeof(Commands)/sizeof(telnet_command_t),
} ;

int main(void)
{
  telnet_start_server(&server) ;
  return 0;  
}

#endif

