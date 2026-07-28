#ifndef EVENT_BUS_H
#define EVENT_BUS_H
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "event_bus_internal.h"
#include "event_bus_types.h"
 /*
 * event_bus 事件总线系统，用于在不同模块之间传递事件和消息。它提供了一个发布-订阅机制，使得模块可以注册事件处理函数，并在事件发生时接收通知。
 *　功能流程：
 * 1、模块注册事件处理函数：模块可以通过调用 event_bus_register_handler 函数来注册一个事件处理函数，并指定感兴趣的事件类型。
 * 2、事件发布：当某个模块发生特定事件时，它可以调用 event_bus_publish_event 函数来发布该事件。事件总线会将该事件发送给所有注册了该事件类型的处理函数。
 * 3、事件处理：注册的事件处理函数会在 event_bus 轮询时被匹配对应的订阅者，调用对应的回调函数。
 * 
 * event_bus 运行在独立线程中，每50ms轮询一次事件队列，检查是否有新的事件需要处理。这样可以确保事件的及时处理，同时不会阻塞主线程的执行。
 */

 


#endif // __EVENT_BUS_H__