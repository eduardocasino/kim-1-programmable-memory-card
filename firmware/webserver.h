#include "httpd.h"

typedef enum { OP_REPLACE, OP_DATA, OP_AND, OP_OR } operation_t;
typedef enum { AC_ENABLE, AC_DISABLE, AC_SETRAM, AC_SETROM } action_t;

err_t handle_ramrom_get( struct http_state *hs );
err_t handle_ramrom_begin( struct http_state *hs, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len,
                       u8_t *code);
err_t handle_ramrom_receive( struct http_state *hs, struct pbuf *p );
err_t handle_ramrom_receive_data( struct http_state *hs, struct pbuf *p );
err_t handle_ramrom_receive_or( struct http_state *hs, struct pbuf *p );
err_t handle_ramrom_receive_and( struct http_state *hs, struct pbuf *p );
void  handle_ramrom_finished( struct http_state *hs, u8_t *code );

err_t handle_ramrom_restore_begin( struct http_state *hs, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len,
                       u8_t *code );
err_t handle_ramrom_enable_begin( struct http_state *hs, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len,
                       u8_t *code );
err_t handle_ramrom_disable_begin( struct http_state *hs, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len,
                       u8_t *code );
err_t handle_ramrom_setrom_begin( struct http_state *hs, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len,
                       u8_t *code );err_t handle_ramrom_actions_receive( struct http_state *hs, struct pbuf *p );
err_t handle_ramrom_setram_begin( struct http_state *hs, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len,
                       u8_t *code );err_t handle_ramrom_actions_receive( struct http_state *hs, struct pbuf *p );
void  handle_ramrom_actions_finished( struct http_state *hs, u8_t *code );

void webserver_run( void );