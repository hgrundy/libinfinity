/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008 Armin Burgmeier <armin@arbur.net>
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
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __INF_COMMUNICATION_GROUP_H__
#define __INF_COMMUNICATION_GROUP_H__

#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/communication/inf-communication-object.h>

#include <glib-object.h>

#include <libxml/tree.h>

G_BEGIN_DECLS

#define INF_COMMUNICATION_TYPE_GROUP                 (inf_communication_group_get_type())
#define INF_COMMUNICATION_GROUP(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_COMMUNICATION_TYPE_GROUP, InfCommunicationGroup))
#define INF_COMMUNICATION_GROUP_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_COMMUNICATION_TYPE_GROUP, InfCommunicationGroupClass))
#define INF_COMMUNICATION_IS_GROUP(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_COMMUNICATION_TYPE_GROUP))
#define INF_COMMUNICATION_IS_GROUP_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_COMMUNICATION_TYPE_GROUP))
#define INF_COMMUNICATION_GROUP_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_COMMUNICATION_TYPE_GROUP, InfCommunicationGroupClass))

typedef struct _InfCommunicationGroup InfCommunicationGroup;
typedef struct _InfCommunicationGroupClass InfCommunicationGroupClass;

/**
 * InfCommunicationGroupClass:
 * @add_member: Default signal handler of the
 * #InfCommunicationGroup::add-member signal.
 * @remove_member: Default signal handler of the
 * #InfCommunicationGroup::remove-member signal.
 * @get_method: Virtual function to determine the method to use
 *
 * The virtual methods and default signal handlers of #InfCommunicationGroup.
 */
struct _InfCommunicationGroupClass {
  /*< private >*/
  GObjectClass parent;

  /*< public >*/
  /* Signals */
  void (*add_member)(InfCommunicationGroup* group,
                     InfXmlConnection* connection);
  void (*remove_member)(InfCommunicationGroup* group,
                        InfXmlConnection* connection);

  /* Virtual functions */
  /* TODO: Should this be const gchar* const* get_methods? */
  const gchar* (*get_method)(InfCommunicationGroup* group,
                             unsigned int index);
};

/**
 * InfCommunicationGroup:
 *
 * #InfCommunicationGroup is an opaque data type. You should only access it
 * via the public API functions.
 */
struct _InfCommunicationGroup {
  /*< private >*/
  GObject parent_instance;
};

GType
inf_communication_group_get_type(void) G_GNUC_CONST;

const gchar*
inf_communication_group_get_name(InfCommunicationGroup* group);

InfCommunicationObject*
inf_communication_group_get_target(InfCommunicationGroup* group);

void
inf_communication_group_set_target(InfCommunicationGroup* group,
                                   InfCommunicationObject* target);

gboolean
inf_communication_group_is_member(InfCommunicationGroup* group,
                                  InfXmlConnection* connection);

void
inf_communication_group_send_message(InfCommunicationGroup* group,
                                     InfXmlConnection* connection,
                                     xmlNodePtr xml);

void
inf_communication_group_send_group_message(InfCommunicationGroup* group,
                                           InfXmlConnection* except,
                                           xmlNodePtr xml);

void
inf_communication_group_cancel_messages(InfCommunicationGroup* group,
                                        InfXmlConnection* connection);

const gchar*
inf_communication_group_get_method_for_network(InfCommunicationGroup* group,
                                               const gchar* network);

const gchar*
inf_communication_group_get_method_for_connection(InfCommunicationGroup* grp,
                                                  InfXmlConnection* conn);

G_END_DECLS

#endif /* __INF_COMMUNICATION_GROUP_H__ */

/* vim:set et sw=2 ts=2: */