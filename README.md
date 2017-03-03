Muxconn
=======

Introduction
------------
Muxconn is an easy to use rpc which use to transfer data between process(not in the same machine). Muxconn includes the function of flow control, connection keepalive.

Install
------------
muxconn only deps libevent2, install libevent2 from http://libevent.org
then you can install Muxconn

``` 
make && make install
```
or
```
make debug && make install
```

How to use
------------

1. Include the header files
------------
More information of the libevent, please refer to the libevent document
``` c
#include <event2/event.h>

#include <mux/mux.h>
#Include <mux/socket.h>

struct event_base *base;

int main () {
  base = event_base_new();
  
  // do you libevent config
  // init_server();
  // or 
  // init_client();
  
  event_base_dispatch(base);

}
```

2. In server side
``` c
void accept_service1(struct mux_socket *client_sock, void *arg) {
  mux_socket_set_callback(sock, service1_readcb, NULL, service1_eventcb, NULL);
  // now you can send any msg to sock with
  // mux_socket_write(client_sock, "you data", data_len);
  // or close the sock when you not need it any more
  // mux_socket_close(client_sock);
}
int init_server() {
  struct mux_listener *listener = mux_server_init(base, "10.1.1.1", 8080);
  if (listener == NULL) {
    return -1;
  }
  mux_listener_set_write_watermask(rpc_listener, 1024000); // set the flow control
  mux_add_acceptcb(listener, "your_service1", accept_service1, NULL);
  // you can add more service with below function, 
  // and different service will only send to its service of client side
}
```

3. In client side

``` c
  void service1_eventcb(struct mux_socket *sock, int event, void *arg) {
    if (event == MUX_EV_CONNECTED) {
      // now you can send any msg to sock with
      // mux_socket_write(client_sock, "you data", data_len);
    } else {
      // if error events happen all you data you had write may lose
    }
  }
  void client_eventcb(struct mux *mux, int event, void *arg) {
    if (event == MUX_EV_CONNECTED) {
      // start listen your service here
      struct mux_socket *sock = mux_socket_new(mux);
      mux_socket_connect(sock, "your_service1");
      mux_socket_set_callback(sock, service1_readcb, NULL, service1_eventcb, NULL);
    } else {
      // stop your service here
    }
  }
  int init_client() {
    struct mux *client = mux_client_init(base, "10.1.1.1", 8080);
    mux_set_write_watermask(client, 1024000);
    mux_client_set_eventcb(client, client_eventcb, NULL);
  }
```
3. Compile your program

```
gcc main.c -o main -lmuxconn
```

