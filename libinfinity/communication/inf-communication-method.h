/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2014 Armin Burgmeier <armin@arbur.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#ifndef __INF_COMMUNICATION_METHOD_H__
#define __INF_COMMUNICATION_METHOD_H__

#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/communication/inf-communication-object.h>

#include <glib-object.h>

#include <libxml/tree.h>

G_BEGIN_DECLS

#define INF_COMMUNICATION_TYPE_METHOD                 (inf_communication_method_get_type())
#define INF_COMMUNICATION_METHOD(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_COMMUNICATION_TYPE_METHOD, InfCommunicationMethod))
#define INF_COMMUNICATION_IS_METHOD(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_COMMUNICATION_TYPE_METHOD))
#define INF_COMMUNICATION_METHOD_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_COMMUNICATION_TYPE_METHOD, InfCommunicationMethodInterface))

/**
 * InfCommunicationMethod:
 *
 * #InfCommunicationMethod is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfCommunicationMethod InfCommunicationMethod;
typedef struct _InfCommunicationMethodInterface InfCommunicationMethodInterface;

/**
 * InfCommunicationMethodInterface:
 * @add_member: Default signal handler of the
 * #InfCommunicationMethod::add-member signal.
 * @remove_member: Default signal handler of the
 * #InfCommunicationMethod::remove-member signal.
 * @is_member: Returns whether the given connection is a member of the group.
 * @send_single: Sends a message to a single connection. Takes ownership of
 * @xml.
 * @send_all: Sends a message to all group members, except @except. Takes
 * ownership of @xml.
 * @cancel_messages: Cancel sending messages that have not yet been sent
 * to the given connection.
 * @received: Handles reception of a message from a registered connection.
 * This normally includes informing a group's NetObject and forwarding the
 * message to other group members.
 * @enqueued: Handles when a message has been enqueued to be sent on a
 * registered connection.
 * @sent: Handles when a message has been sent to a registered connection.
 *
 * The default signal handlers of virtual methods of #InfCommunicationMethod.
 * These implement communication within a #InfCommunicationGroup.
 */
struct _InfCommunicationMethodInterface {
  /*< private >*/
  GTypeInterface parent;

  /*< public >*/
  /* Signals */
  void (*add_member)(InfCommunicationMethod* method,
                     InfXmlConnection* connection);
  void (*remove_member)(InfCommunicationMethod* method,
                        InfXmlConnection* connection);

  /* Virtual functions */
  gboolean (*is_member)(InfCommunicationMethod* method,
                        InfXmlConnection* connection);

  void (*send_single)(InfCommunicationMethod* method,
                      InfXmlConnection* connection,
                      xmlNodePtr xml);
  void (*send_all)(InfCommunicationMethod* method,
                   xmlNodePtr xml);
  void (*cancel_messages)(InfCommunicationMethod* method,
                          InfXmlConnection* connection);

  InfCommunicationScope (*received)(InfCommunicationMethod* method,
                                    InfXmlConnection* connection,
                                    xmlNodePtr xml);
  void (*enqueued)(InfCommunicationMethod* method,
                   InfXmlConnection* connection,
                   xmlNodePtr xml);
  void (*sent)(InfCommunicationMethod* method,
               InfXmlConnection* connection,
               xmlNodePtr xml);
};

GType
inf_communication_method_get_type(void) G_GNUC_CONST;

void
inf_communication_method_add_member(InfCommunicationMethod* method,
                                    InfXmlConnection* connection);

void
inf_communication_method_remove_member(InfCommunicationMethod* method,
                                       InfXmlConnection* connection);

gboolean
inf_communication_method_is_member(InfCommunicationMethod* method,
                                   InfXmlConnection* connection);

void
inf_communication_method_send_single(InfCommunicationMethod* method,
                                     InfXmlConnection* connection,
                                     xmlNodePtr xml);

void
inf_communication_method_send_all(InfCommunicationMethod* method,
                                  xmlNodePtr xml);

void
inf_communication_method_cancel_messages(InfCommunicationMethod* method,
                                         InfXmlConnection* connection);

InfCommunicationScope
inf_communication_method_received(InfCommunicationMethod* method,
                                  InfXmlConnection* connection,
                                  xmlNodePtr xml);

void
inf_communication_method_enqueued(InfCommunicationMethod* method,
                                  InfXmlConnection* connection,
                                  xmlNodePtr xml);

void
inf_communication_method_sent(InfCommunicationMethod* method,
                              InfXmlConnection* connection,
                              xmlNodePtr xml);

G_END_DECLS

#endif /* __INF_COMMUNICATION_METHOD_H__ */

/* vim:set et sw=2 ts=2: */
