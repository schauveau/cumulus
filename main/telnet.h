#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define CRLF "\r\n"

#define TELNET_DEFAULT_PORT 23

struct telnet_server_t ;
struct telnet_command_t ;

// Encode a argument to a telnet command.
//
// The pointer if any, should NOT be freed and is only valid
// during the duration the command callback.
//
typedef union telnet_argval_t {
  const char *s; // for arguments of kinds 'w' and 'r'
  int32_t     i; // for arguments of kinds 'i' and 'b'
  float       f; // for arguments of kind 'f'
} telnet_argval_t;

typedef void (*telnet_callback_t)(struct telnet_server_t *server, const struct telnet_command_t *cmd, int narg, const telnet_argval_t *arg) ;

// Describe a telnet command. 
//  
// The characters in .args have the following meaning:
//   - 'b' for a boolean (on/off, true/false, 0/1)
//   - 'w' for a word (i.e. a string without blanks)
//   - 'r' for a raw string until the end of the command
//   - 'i' for an integer value
//   - 'f' for a floating point value 
//   - '?' to indicate the start of the optional arguments
//
// By design, 'r' can only be the last argument. 
//
// For example, if .args is "i?fr" then the first argument
// is an integer. The second is optional and must be a floating
// point value. The third arguments is also optional and
// contains the rest of the command. 
//
typedef struct telnet_command_t {
  int id;             // A user-defined identifier. 
  const char *name;   // the command name
  const char *args;   // describe the expected arguments
  telnet_callback_t callback;
  const char *help[3]; // First is short description, second is args description and third is detailed help.
} telnet_command_t;

typedef struct telnet_server_t {
  // ==== The members below must be configured ====   
  int port;
  telnet_command_t * commands;
  int                command_count;  // length of .commands[]
  // ==== The members below are private ====   
  bool connected;  // true while connected to a client
  int sock;   // The socket number when connected
} telnet_server_t; 

// Callbacks for the default commands.
void telnet_cmd_help(telnet_server_t *server, const telnet_command_t *cmd, int narg, const telnet_argval_t *arg) ;
void telnet_cmd_quit(telnet_server_t *server, const telnet_command_t *cmd, int narg, const telnet_argval_t *arg) ;

// A simple command callback that print a "not yet implemented" error message
void telnet_cmd_not_implemented(telnet_server_t *server, const telnet_command_t *cmd, int narg, const telnet_argval_t *arg) ;

// Start the telnet server
void telnet_start_server( telnet_server_t*server);

// Send a string to the current telnet client
//
//  - a \r is inserted before each \n
//
// In case of success, return the number of characters sent to the client.
// In case of success, return a negative error code.
//
int telnet_send_string(telnet_server_t *server, const char *str);

// Send a raw buffer to the telnet client
//
// Return the number of character sent or a negative a value if a problem occured. 
// 
// If len is negative then nothing is sent and 0 is returned. 
//
int telnet_send_raw(telnet_server_t *server, const char *data, int len);

//  Send a buffer to the telnet client
//
//   - all \0 characters are discarded
//   - a \r is inserted before each \n
//
// In case of success, return the number of characters sent to the client.
// In case of success, return a negative error code. 
//
int telnet_send_buffer(telnet_server_t *server, const char *data, int len);

// Send a formated string to the telnet client.
//
//   - all \0 characters are discarded
//   - a \r is inserted before each \n
//
// In case of success, return the number of characters sent to the client.
// In case of failure, return a negative value. 
//
int telnet_printf(telnet_server_t *server, const char *format, ...);


#define TELNET_HELP_CMD \
  {\
   .name="help",\
   .args="?w",\
   .callback = &telnet_cmd_help, \
   .help = {\
     "List all commands or display help for a specific command",\
     "[COMMAND]"\
   }\
  }

#define TELNET_QUIT_CMD \
  {\
   .name="quit",\
   .args="",\
   .callback = &telnet_cmd_quit, \
   .help = {\
     "Close the connection"\
   }\
  }

#define TELNET_DEFAULT_COMMANDS \
  TELNET_HELP_CMD, \
  TELNET_QUIT_CMD

#ifdef __cplusplus
} // extern "C"
#endif

