/* infinote - Collaborative notetaking application
 * Copyright (C) 2007 Armin Burgmeier
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

#include <libinfgtk/inf-gtk-browser-view.h>
#include <libinfinity/inf-marshal.h>

#include <gtk/gtktreeview.h>
#include <gtk/gtktreeviewcolumn.h>
#include <gtk/gtkcellrendererpixbuf.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkcellrendererprogress.h>
#include <gtk/gtkstock.h>

#define INF_GTK_BROWSER_VIEW_INITIAL_EXPLORATION \
  "inf-gtk-browser-view-initial-exploration"

typedef struct _InfGtkBrowserViewObject InfGtkBrowserViewObject;
struct _InfGtkBrowserViewObject {
  GObject* object;
  GtkTreeRowReference* reference;

  /* This is valid as long as the TreeRowReference above is valid, but we
   * still need the TreeRowReference to know when it becomes invalid */
  GtkTreeIter iter;
};

typedef struct _InfGtkBrowserViewPrivate InfGtkBrowserViewPrivate;
struct _InfGtkBrowserViewPrivate {
  GtkWidget* treeview;
  GtkTreeViewColumn* column;

  /* Note that progress and status_text are never visible at the same time */
  GtkCellRenderer* renderer_icon;
  GtkCellRenderer* renderer_status_icon; /* toplevel only */
  GtkCellRenderer* renderer_name;
  GtkCellRenderer* renderer_progress;
  GtkCellRenderer* renderer_status;

  GArray* browsers;
  GArray* explore_requests;
};

enum {
  PROP_0,

  PROP_MODEL
};

#define INF_GTK_BROWSER_VIEW_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_GTK_TYPE_BROWSER_VIEW, InfGtkBrowserViewPrivate))

/* We do some rather complex stuff here because we don't get the iter when
 * a row is deleted. This would be nice to disconnect browser signals for
 * example (we need the iter to access the browser to disconnect the signals),
 * but it is not possible.
 *
 * Instead, we keep an array of browsers in the model including
 * TreeRowReferences where they are in the tree. When a row is removed, we
 * check which TreeRowReferences got invalid and delete the corresponding
 * browsers from our array. The same holds for explore requests. */

static GObjectClass* parent_class;

/* Lookup the InfGtkBrowserViewObject index in the priv->explore_requests
 * array for the given request. */
gint
inf_gtk_browser_view_explore_request_find(InfGtkBrowserView* view,
                                          InfcExploreRequest* request)
{
  InfGtkBrowserViewPrivate* priv;
  InfGtkBrowserViewObject* object;
  guint i;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  for(i = 0; i < priv->explore_requests->len; ++ i)
  {
    object = &g_array_index(
      priv->explore_requests,
      InfGtkBrowserViewObject,
      i
    );

    if(object->object == G_OBJECT(request))
      return i;
  }

  return -1;
}

static void
inf_gtk_browser_view_redraw_row(InfGtkBrowserView* view,
                                GtkTreePath* path,
                                GtkTreeIter* iter)
{
  /* TODO: Is there a better way to do this? Calling gtk_tree_model_changed is
   * not good:
   *
   * The actual data in the model has not been changed, otherwise the model
   * would have emitted the signal. What actually has changed is just what we
   * display, for example the progress bar of the exploration of a node. This
   * does not belong to the model because the model does not care about
   * exploration progress, but we want to show it to the user nevertheless.
   * I am not sure whether this is a problem in our design or a limitation
   * in the GTK+ treeview and friends. */
  gtk_tree_model_row_changed(
    gtk_tree_view_get_model(
      GTK_TREE_VIEW(INF_GTK_BROWSER_VIEW_PRIVATE(view)->treeview)
    ),
    path,
    iter
  );
}

static void
inf_gtk_browser_view_redraw_node_for_explore_request(InfGtkBrowserView* view,
                                                     InfcExploreRequest* req)
{
  InfGtkBrowserViewPrivate* priv;
  InfGtkBrowserViewObject* object;
  GtkTreePath* path;
  gint i;

  /* We could get the iter easily by querying the InfcBrowserIter with
   * infc_browser_iter_from_explore_request and then obtaining the GtkTreeIter
   * with inf_gtk_browser_model_browser_iter_to_tree_iter. However, we do not
   * get the path this way and gtk_tree_model_get_path is expensive.
   * Therefore, we lookup the InfGtkBrowserViewObject which has both iter
   * and path (via the GtkTreeRowReference). */

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);
  i = inf_gtk_browser_view_explore_request_find(view, req);
  g_assert(i >= 0);

  object = &g_array_index(priv->explore_requests, InfGtkBrowserViewObject, i);
  path = gtk_tree_row_reference_get_path(object->reference);
  g_assert(path != NULL);

  inf_gtk_browser_view_redraw_row(view, path, &object->iter);
  gtk_tree_path_free(path);
}

static void
inf_gtk_browser_view_explore_request_initiated_cb(InfcExploreRequest* request,
                                                  guint total,
                                                  gpointer user_data)
{
  inf_gtk_browser_view_redraw_node_for_explore_request(
    INF_GTK_BROWSER_VIEW(user_data),
    request
  );
}

static void
inf_gtk_browser_view_explore_request_progress_cb(InfcExploreRequest* request,
                                                 guint current,
                                                 guint total,
                                                 gpointer user_data)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;
  InfGtkBrowserViewObject* object;
  GtkTreePath* path;
  gpointer initial_exploration;
  gint i;

  view = INF_GTK_BROWSER_VIEW(user_data);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  i = inf_gtk_browser_view_explore_request_find(view, request);
  g_assert(i >= 0);

  object = &g_array_index(priv->explore_requests, InfGtkBrowserViewObject, i);
  path = gtk_tree_row_reference_get_path(object->reference);
  g_assert(path != NULL);

  inf_gtk_browser_view_redraw_row(view, path, &object->iter);

  initial_exploration = g_object_get_data(
    G_OBJECT(request),
    INF_GTK_BROWSER_VIEW_INITIAL_EXPLORATION
  );

  /* Expand initial exploration of the root node bcesaue the user double
   * clicked on it to connect, so he wants most likely to see the remote
   * directory. */
  if(GPOINTER_TO_UINT(initial_exploration))
  {
    g_object_set_data(
      G_OBJECT(request),
      INF_GTK_BROWSER_VIEW_INITIAL_EXPLORATION,
      GUINT_TO_POINTER(0)
    );

    gtk_tree_view_expand_row(GTK_TREE_VIEW(priv->treeview), path, FALSE);
  }

  gtk_tree_path_free(path);
}

/* Required by inf_gtk_browser_view_explore_request_finished_cb */
static void
inf_gtk_browser_view_explore_request_removed(InfGtkBrowserView* view,
                                             guint i);

static void
inf_gtk_browser_view_explore_request_finished_cb(InfcExploreRequest* request,
                                                 gpointer user_data)
{
  InfGtkBrowserView* view;
  gint i;

  view = INF_GTK_BROWSER_VIEW(user_data);
  i = inf_gtk_browser_view_explore_request_find(view, request);
  g_assert(i >= 0);

  inf_gtk_browser_view_explore_request_removed(view, i);
}

static void
inf_gtk_browser_view_explore_request_added(InfGtkBrowserView* view,
                                           GtkTreePath* path,
                                           GtkTreeIter* iter,
                                           InfcExploreRequest* request)
{
  InfGtkBrowserViewPrivate* priv;
  InfGtkBrowserViewObject object;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);
  g_assert(inf_gtk_browser_view_explore_request_find(view, request) == -1);
  
  object.object = G_OBJECT(request);
  g_object_ref(G_OBJECT(request));

  object.reference = gtk_tree_row_reference_new_proxy(
    G_OBJECT(priv->column),
    gtk_tree_view_get_model(GTK_TREE_VIEW(priv->treeview)),
    path
  );

  g_assert(object.reference != NULL);

  object.iter = *iter;
  g_array_append_vals(priv->explore_requests, &object, 1);

  g_signal_connect_after(
    G_OBJECT(request),
    "initiated",
    G_CALLBACK(inf_gtk_browser_view_explore_request_initiated_cb),
    view
  );

  g_signal_connect_after(
    G_OBJECT(request),
    "progress",
    G_CALLBACK(inf_gtk_browser_view_explore_request_progress_cb),
    view
  );

  g_signal_connect_after(
    G_OBJECT(request),
    "finished",
    G_CALLBACK(inf_gtk_browser_view_explore_request_finished_cb),
    view
  );

  inf_gtk_browser_view_redraw_row(view, path, iter);
}

/* Just free data allocated by object */
static void
inf_gtk_browser_view_explore_request_free(InfGtkBrowserView* view,
                                          InfGtkBrowserViewObject* object)
{
  g_assert(INFC_IS_EXPLORE_REQUEST(object->object));

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(object->object),
    G_CALLBACK(inf_gtk_browser_view_explore_request_initiated_cb),
    view
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(object->object),
    G_CALLBACK(inf_gtk_browser_view_explore_request_progress_cb),
    view
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(object->object),
    G_CALLBACK(inf_gtk_browser_view_explore_request_finished_cb),
    view
  );

  gtk_tree_row_reference_free(object->reference);
  g_object_unref(G_OBJECT(object->object));
}

/* Unlink from view */
static void
inf_gtk_browser_view_explore_request_removed(InfGtkBrowserView* view,
                                             guint i)
{
  InfGtkBrowserViewPrivate* priv;
  InfGtkBrowserViewObject* object;
  GtkTreePath* path;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);
  object = &g_array_index(priv->explore_requests, InfGtkBrowserViewObject, i);

  /* Redraw if the reference is still valid. Note that if the node is removed
   * while being explored the reference is not valid at this point. */
  path = gtk_tree_row_reference_get_path(object->reference);
  if(path != NULL)
  {
    inf_gtk_browser_view_redraw_row(view, path, &object->iter);
    gtk_tree_path_free(path);
  }

  inf_gtk_browser_view_explore_request_free(view, object);
  g_array_remove_index_fast(priv->explore_requests, i);
}

static void
inf_gtk_browser_view_begin_explore_cb(InfcBrowser* browser,
                                      InfcBrowserIter* iter,
                                      InfcExploreRequest* request,
                                      gpointer user_data)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;
  GtkTreeModel* model;
  GtkTreeIter tree_iter;
  GtkTreePath* path;
  gboolean result;

  view = INF_GTK_BROWSER_VIEW(user_data);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(priv->treeview));

  result = inf_gtk_browser_model_browser_iter_to_tree_iter(
    INF_GTK_BROWSER_MODEL(model),
    browser,
    iter,
    &tree_iter
  );

  g_assert(result == TRUE);

  path = gtk_tree_model_get_path(model, &tree_iter);
  inf_gtk_browser_view_explore_request_added(view, path, &tree_iter, request);
  gtk_tree_path_free(path); 
}

/* This function recursively walks down iter and all its children and
 * inserts running explore requests into the view. */
static void
inf_gtk_browser_view_walk_explore_requests(InfGtkBrowserView* view,
                                           InfcBrowser* browser,
                                           InfcBrowserIter* iter)
{
  InfGtkBrowserViewPrivate* priv;
  InfcExploreRequest* request;
  GtkTreeModel* model;
  GtkTreeIter tree_iter;
  GtkTreePath* path;
  InfcBrowserIter child_iter;
  gboolean result;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  if(infc_browser_iter_get_explored(browser, iter))
  {
    child_iter = *iter;
    for(result = infc_browser_iter_get_child(browser, &child_iter);
        result == TRUE;
        result = infc_browser_iter_get_next(browser, &child_iter))
    {
      inf_gtk_browser_view_walk_explore_requests(view, browser, &child_iter);
    }
  }

  request = infc_browser_iter_get_explore_request(browser, iter);
  if(request != NULL)
  {
    model = gtk_tree_view_get_model(GTK_TREE_VIEW(priv->treeview));

    result = inf_gtk_browser_model_browser_iter_to_tree_iter(
      INF_GTK_BROWSER_MODEL(model),
      browser,
      iter,
      &tree_iter
    );

    path = gtk_tree_model_get_path(model, &tree_iter);

    inf_gtk_browser_view_explore_request_added(
      view,
      path,
      &tree_iter,
      request
    );

    gtk_tree_path_free(path);
  }
}

static void
inf_gtk_browser_view_initial_root_explore(InfGtkBrowserView* view,
                                          InfcBrowser* browser,
                                          InfcBrowserIter* browser_iter)
{
  InfcExploreRequest* request;

  /* Explore root node if it is not already explored */
  if(infc_browser_iter_get_explored(browser, browser_iter) == FALSE &&
     infc_browser_iter_get_explore_request(browser, browser_iter) == NULL)
  {
    request = infc_browser_iter_explore(browser, browser_iter);

    g_object_set_data(
      G_OBJECT(request),
      INF_GTK_BROWSER_VIEW_INITIAL_EXPLORATION,
      GUINT_TO_POINTER(1)
    );
  }
}

/* Called whenever a browser is added to a node. The browser is expected
 * to be already refed. */
static void
inf_gtk_browser_view_browser_added(InfGtkBrowserView* view,
                                   GtkTreePath* path,
                                   GtkTreeIter* iter,
                                   InfcBrowser* browser)
{
  InfGtkBrowserViewPrivate* priv;
  InfGtkBrowserViewObject object;
  GtkTreeModel* model;
  InfXmlConnection* connection;
  InfXmlConnectionStatus status;
  InfcBrowserIter* browser_iter;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(priv->treeview));
  object.object = G_OBJECT(browser);

  object.reference = gtk_tree_row_reference_new_proxy(
    G_OBJECT(priv->column),
    model,
    path
  );

  object.iter = *iter;
  g_array_append_vals(priv->browsers, &object, 1);

  g_signal_connect(
    G_OBJECT(browser),
    "begin-explore",
    G_CALLBACK(inf_gtk_browser_view_begin_explore_cb),
    view
  );

  connection = infc_browser_get_connection(browser);
  g_object_get(G_OBJECT(connection), "status", &status, NULL);

  /* Initial explore if connection is already open */
  if(status == INF_XML_CONNECTION_OPEN)
  {
    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_NODE, &browser_iter,
      -1
    );

    /* Look for running explore requests, insert into array of running
     * explore requests to show their progress. */
    /* TODO: We do not need this anymore when we get insertion callbacks
     * from the model for each node in the newly added browser. See
     * inf-gtk-browser-model.c:292. */
    inf_gtk_browser_view_walk_explore_requests(view, browser, browser_iter);

    /* Explore root node initially if not already explored */
    inf_gtk_browser_view_initial_root_explore(view, browser, browser_iter);

    infc_browser_iter_free(browser_iter);
  }
}

static void
inf_gtk_browser_view_browser_free(InfGtkBrowserView* view,
                                  InfGtkBrowserViewObject* object)
{
  g_signal_handlers_disconnect_by_func(
    G_OBJECT(object->object),
    G_CALLBACK(inf_gtk_browser_view_begin_explore_cb),
    view
  );

  gtk_tree_row_reference_free(object->reference);
  g_object_unref(G_OBJECT(object->object));
}

static void
inf_gtk_browser_view_browser_removed(InfGtkBrowserView* view,
                                     guint index)
{
  InfGtkBrowserViewPrivate* priv;
  InfGtkBrowserViewObject* object;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);
  object = &g_array_index(priv->browsers, InfGtkBrowserViewObject, index);

  /* TODO: Also remove any explore requests belonging to this browser */

  inf_gtk_browser_view_browser_free(view, object);
  g_array_remove_index_fast(priv->browsers, index);
}

static void
inf_gtk_browser_view_row_inserted_cb(GtkTreeModel* model,
                                     GtkTreePath* path,
                                     GtkTreeIter* iter,
                                     gpointer user_data)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;
  GtkTreeIter parent_iter;
  InfcBrowser* browser;
  InfcBrowserIter* browser_iter;
  GtkTreePath* parent_path;
  GtkTreeView* treeview;

  view = INF_GTK_BROWSER_VIEW(user_data);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  gtk_tree_row_reference_inserted(G_OBJECT(priv->column), path);

  if(gtk_tree_model_iter_parent(model, &parent_iter, iter) == FALSE)
  {
    /* No parent, so iter is top-level. Check if
     * it has a browser associated. */
    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_BROWSER,
      &browser,
      -1
    );

    if(browser != NULL)
      inf_gtk_browser_view_browser_added(view, path, iter, browser);
  }
  else
  {
    /* Inner node. Explore if the parent node is expanded. */
    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_BROWSER, &browser,
      INF_GTK_BROWSER_MODEL_COL_NODE, &browser_iter,
      -1
    );

    g_assert(browser != NULL);

    if(infc_browser_iter_is_subdirectory(browser, browser_iter))
    {
      /* TODO: Add explore request to array if this row is currently being
       * explored. */
      /* Perhaps some other code already explored this. */
      if(infc_browser_iter_get_explored(browser, browser_iter) == FALSE &&
         infc_browser_iter_get_explore_request(browser, browser_iter) == NULL)
      {
        treeview = GTK_TREE_VIEW(priv->treeview);

        parent_path = gtk_tree_path_copy(path);
        gtk_tree_path_up(parent_path);

        if(gtk_tree_view_row_expanded(treeview, parent_path))
          infc_browser_iter_explore(browser, browser_iter);

        gtk_tree_path_free(parent_path);
      }
    }

    infc_browser_iter_free(browser_iter);
    g_object_unref(G_OBJECT(browser));
  }
}

static void
inf_gtk_browser_view_row_changed_cb(GtkTreeModel* model,
                                    GtkTreePath* path,
                                    GtkTreeIter* iter,
                                    gpointer user_data)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;
  GtkTreeIter parent_iter;
  InfGtkBrowserModelStatus status;

  InfcBrowser* browser;
  InfcBrowserIter* browser_iter;
  InfGtkBrowserViewObject* object;
  GtkTreePath* object_path;
  guint i;

  view = INF_GTK_BROWSER_VIEW(user_data);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  if(gtk_tree_model_iter_parent(model, &parent_iter, iter) == FALSE)
  {
    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_BROWSER, &browser,
      INF_GTK_BROWSER_MODEL_COL_STATUS, &status,
      -1
    );
    
    if(status == INF_GTK_BROWSER_MODEL_CONNECTED)
    {
      gtk_tree_model_get(
        model,
        iter,
        INF_GTK_BROWSER_MODEL_COL_NODE, &browser_iter,
        -1
      );

      inf_gtk_browser_view_initial_root_explore(view, browser, browser_iter);
      infc_browser_iter_free(browser_iter);
    }

    for(i = 0; i < priv->browsers->len; ++ i)
    {
      object = &g_array_index(priv->browsers, InfGtkBrowserViewObject, i);
      object_path = gtk_tree_row_reference_get_path(object->reference);
      if(gtk_tree_path_compare(path, object_path) == 0)
      {
        gtk_tree_path_free(object_path);
        break;
      }

      gtk_tree_path_free(object_path);
    }

    if(browser == NULL && i < priv->browsers->len)
      inf_gtk_browser_view_browser_removed(view, i);
    else if(browser != NULL && i == priv->browsers->len)
      inf_gtk_browser_view_browser_added(view, path, iter, browser);
    else if(browser != NULL) /* already added */
      g_object_unref(G_OBJECT(browser));
  }
}

static void
inf_gtk_browser_view_row_deleted_cb(GtkTreeModel* model,
                                    GtkTreePath* path,
                                    gpointer user_data)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;
  InfGtkBrowserViewObject* object;
  guint i;

  view = INF_GTK_BROWSER_VIEW(user_data);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  gtk_tree_row_reference_deleted(G_OBJECT(priv->column), path);

  /* Check for references that became invalid */
  if(gtk_tree_path_get_depth(path) == 1)
  {
    /* Toplevel, so browsers may be affected */
    for(i = 0; i < priv->browsers->len; ++ i)
    {
      object = &g_array_index(priv->browsers, InfGtkBrowserViewObject, i);
      if(gtk_tree_row_reference_valid(object->reference) == FALSE)
      {
        /* Browser node was removed */
        inf_gtk_browser_view_browser_removed(view, i);
      }
    }
  }

  /* Explore requests may be affected as well */
  for(i = 0; i < priv->explore_requests->len; ++ i)
  {
    object = &g_array_index(
      priv->explore_requests,
      InfGtkBrowserViewObject,
      i
    );

    if(gtk_tree_row_reference_valid(object->reference) == FALSE)
    {
      inf_gtk_browser_view_explore_request_removed(view, i);
    }
  }
}

static void
inf_gtk_browser_view_rows_reordered_cb(GtkTreeModel* model,
                                       GtkTreePath* path,
                                       GtkTreeIter* iter,
                                       gint* new_order,
                                       gpointer user_data)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;

  view = INF_GTK_BROWSER_VIEW(user_data);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  gtk_tree_row_reference_reordered(
    G_OBJECT(priv->column),
    path,
    iter,
    new_order
  );
}

static void
inf_gtk_browser_view_set_model(InfGtkBrowserView* view,
                               InfGtkBrowserModel* model)
{
  InfGtkBrowserViewPrivate* priv;
  InfGtkBrowserViewObject* object;
  GtkTreeModel* current_model;
  GtkTreeIter iter;
  guint i;
  InfcBrowser* browser;
  GtkTreePath* path;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);
  current_model = gtk_tree_view_get_model(GTK_TREE_VIEW(priv->treeview));

  if(current_model != NULL)
  {
    if(priv->explore_requests->len > 0)
    {
      for(i = 0; i < priv->explore_requests->len; ++ i)
      {
        object = &g_array_index(
          priv->explore_requests,
          InfGtkBrowserViewObject,
          i
        );

        inf_gtk_browser_view_explore_request_free(view, object);
      }

      g_array_remove_range(
        priv->explore_requests,
        0,
        priv->explore_requests->len
      );
    }

    if(priv->browsers->len > 0)
    {
      for(i = 0; i < priv->browsers->len; ++ i)
      {
        object = &g_array_index(priv->browsers, InfGtkBrowserViewObject, i);
        inf_gtk_browser_view_browser_free(view, object);
      }

      g_array_remove_range(priv->browsers, 0, priv->browsers->len);
    }

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(current_model),
      G_CALLBACK(inf_gtk_browser_view_row_inserted_cb),
      view
    );

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(current_model),
      G_CALLBACK(inf_gtk_browser_view_row_deleted_cb),
      view
    );

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(current_model),
      G_CALLBACK(inf_gtk_browser_view_row_changed_cb),
      view
    );

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(current_model),
      G_CALLBACK(inf_gtk_browser_view_rows_reordered_cb),
      view
    );
  }

  gtk_tree_view_set_model(
    GTK_TREE_VIEW(priv->treeview),
    GTK_TREE_MODEL(model)
  );

  if(model != NULL)
  {
    /* Add initial browsers */
    if(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(model), &iter) == TRUE)
    {
      path = gtk_tree_path_new_first();

      do
      {
        gtk_tree_model_get(
          GTK_TREE_MODEL(model),
          &iter,
          INF_GTK_BROWSER_MODEL_COL_BROWSER,
          &browser,
          -1
        );

        if(browser != NULL)
          inf_gtk_browser_view_browser_added(view, path, &iter, browser);

        gtk_tree_path_next(path);
      } while(gtk_tree_model_iter_next(GTK_TREE_MODEL(model), &iter) == TRUE);
    }

    g_signal_connect(
      G_OBJECT(model),
      "row-inserted",
      G_CALLBACK(inf_gtk_browser_view_row_inserted_cb),
      view
    );

    g_signal_connect(
      G_OBJECT(model),
      "row-deleted",
      G_CALLBACK(inf_gtk_browser_view_row_deleted_cb),
      view
    );

    g_signal_connect(
      G_OBJECT(model),
      "row-changed",
      G_CALLBACK(inf_gtk_browser_view_row_changed_cb),
      view
    );

    g_signal_connect(
      G_OBJECT(model),
      "rows-reordered",
      G_CALLBACK(inf_gtk_browser_view_rows_reordered_cb),
      view
    );
  }
}

static void
inf_gtk_browser_view_row_expanded_cb(GtkTreeView* tree_view,
                                     GtkTreeIter* iter,
                                     GtkTreePath* path,
                                     gpointer user_data)
{
  GtkTreeModel* model;
  InfcBrowser* browser;
  InfcBrowserIter* browser_iter;

  model = gtk_tree_view_get_model(tree_view);

  gtk_tree_model_get(
    model,
    iter,
    INF_GTK_BROWSER_MODEL_COL_BROWSER, &browser,
    INF_GTK_BROWSER_MODEL_COL_NODE, &browser_iter,
    -1
  );

  g_assert(browser != NULL);

  /* Explore all child nodes that are not yet explored */
  if(infc_browser_iter_get_child(browser, browser_iter))
  {
    do
    {
      if(infc_browser_iter_is_subdirectory(browser, browser_iter) == TRUE &&
         infc_browser_iter_get_explored(browser, browser_iter) == FALSE &&
         infc_browser_iter_get_explore_request(browser, browser_iter) == NULL)
      {
        infc_browser_iter_explore(browser, browser_iter);
      }
    } while(infc_browser_iter_get_next(browser, browser_iter));
  }

  infc_browser_iter_free(browser_iter);
  g_object_unref(G_OBJECT(browser));
}

static void
inf_gtk_browser_view_row_activated_cb(GtkTreeView* tree_view,
                                      GtkTreePath* path,
                                      GtkTreeViewColumn* column,
                                      gpointer user_data)
{
  GtkTreeModel* model;
  InfGtkBrowserModelStatus status;
  InfDiscovery* discovery;
  InfDiscoveryInfo* info;
  GtkTreeIter iter;

  /* Connect to host, if not already */
  if(gtk_tree_path_get_depth(path) == 1)
  {
    model = gtk_tree_view_get_model(tree_view);
    gtk_tree_model_get_iter(model, &iter, path);

    gtk_tree_model_get(
      model,
      &iter,
      INF_GTK_BROWSER_MODEL_COL_STATUS, &status,
      INF_GTK_BROWSER_MODEL_COL_DISCOVERY, &discovery,
      INF_GTK_BROWSER_MODEL_COL_DISCOVERY_INFO, &info,
      -1
    );

    if(discovery != NULL)
    {
      if(status == INF_GTK_BROWSER_MODEL_DISCOVERED)
      {
        inf_gtk_browser_model_resolve(
          INF_GTK_BROWSER_MODEL(model),
          discovery,
          info
        );
      }

      g_object_unref(G_OBJECT(discovery));
    }
  }
}

static void
inf_gtk_browser_view_icon_data_func(GtkTreeViewColumn* column,
                                    GtkCellRenderer* renderer,
                                    GtkTreeModel* model,
                                    GtkTreeIter* iter,
                                    gpointer user_data)
{
  GtkTreeIter iter_parent;
  InfDiscovery* discovery;
  InfcBrowser* browser;
  InfcBrowserIter* browser_iter;

  if(gtk_tree_model_iter_parent(model, &iter_parent, iter))
  {
    /* Inner node */
    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_BROWSER, &browser,
      INF_GTK_BROWSER_MODEL_COL_NODE, &browser_iter,
      -1
    );

    /* TODO: Set icon depending on note type, perhaps also on whether
     * we are subscribed or not. */
    if(infc_browser_iter_is_subdirectory(browser, browser_iter))
      g_object_set(G_OBJECT(renderer), "stock-id", GTK_STOCK_DIRECTORY, NULL);
    else
      g_object_set(G_OBJECT(renderer), "stock-id", GTK_STOCK_FILE, NULL);

    infc_browser_iter_free(browser_iter);
    g_object_unref(G_OBJECT(browser));
  }
  else
  {
    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_DISCOVERY, &discovery,
      INF_GTK_BROWSER_MODEL_COL_BROWSER, &browser,
      -1
    );

    /* TODO: Set icon depending on discovery type (LAN, jabber, direct) */
    g_object_set(G_OBJECT(renderer), "stock-id", GTK_STOCK_NETWORK, NULL);

    if(discovery != NULL) g_object_unref(G_OBJECT(discovery));
    if(browser != NULL) g_object_unref(G_OBJECT(browser));
  }
}

static void
inf_gtk_browser_view_status_icon_data_func(GtkTreeViewColumn* column,
                                           GtkCellRenderer* renderer,
                                           GtkTreeModel* model,
                                           GtkTreeIter* iter,
                                           gpointer user_data)
{
  GtkTreeIter iter_parent;
  InfGtkBrowserModelStatus status;
  const gchar* stock_id;

  if(gtk_tree_model_iter_parent(model, &iter_parent, iter))
  {
    /* inner node, ignore */
    g_object_set(G_OBJECT(renderer), "visible", FALSE, NULL);
  }
  else
  {
    /* toplevel */
    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_STATUS, &status,
      -1
    );

    switch(status)
    {
    case INF_GTK_BROWSER_MODEL_DISCOVERED:
    case INF_GTK_BROWSER_MODEL_RESOLVING:
    case INF_GTK_BROWSER_MODEL_CONNECTING:
      stock_id = GTK_STOCK_DISCONNECT;
      break;
    case INF_GTK_BROWSER_MODEL_CONNECTED:
      stock_id = GTK_STOCK_CONNECT;
      break;
    case INF_GTK_BROWSER_MODEL_ERROR:
      stock_id = GTK_STOCK_DIALOG_ERROR;
      break;
    default:
      g_assert_not_reached();
      break;
    }
    
    g_object_set(
      G_OBJECT(renderer),
      "visible", TRUE,
      "stock-id", stock_id,
      NULL
    );
  }
}

static void
inf_gtk_browser_view_name_data_func(GtkTreeViewColumn* column,
                                    GtkCellRenderer* renderer,
                                    GtkTreeModel* model,
                                    GtkTreeIter* iter,
                                    gpointer user_data)
{
  GtkTreeIter iter_parent;
  InfDiscovery* discovery;
  InfDiscoveryInfo* info;
  InfcBrowser* browser;
  InfcBrowserIter* browser_iter;
  const gchar* name;
  gchar* service_name;

  if(gtk_tree_model_iter_parent(model, &iter_parent, iter))
  {
    /* Inner node */
    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_BROWSER, &browser,
      INF_GTK_BROWSER_MODEL_COL_NODE, &browser_iter,
      -1
    );

    name = infc_browser_iter_get_name(browser, browser_iter);
    g_object_set(G_OBJECT(renderer), "text", name, NULL);

    infc_browser_iter_free(browser_iter);
    g_object_unref(G_OBJECT(browser));
  }
  else
  {
    /* Toplevel */
    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_DISCOVERY, &discovery,
      INF_GTK_BROWSER_MODEL_COL_DISCOVERY_INFO, &info,
      -1
    );

    if(discovery != NULL)
    {
      g_assert(info != NULL);

      service_name = inf_discovery_info_get_service_name(discovery, info);
      g_object_set(G_OBJECT(renderer), "text", service_name, NULL);
      g_free(service_name);

      g_object_unref(G_OBJECT(discovery));
    }
    else
    {
      /* TODO: Display remote address */
      g_object_set(G_OBJECT(renderer), "text", "Direct connection", NULL);
    }
  }
}

static void
inf_gtk_browser_view_progress_data_func(GtkTreeViewColumn* column,
                                        GtkCellRenderer* renderer,
                                        GtkTreeModel* model,
                                        GtkTreeIter* iter,
                                        gpointer user_data)
{
  InfcBrowser* browser;
  InfcBrowserIter* browser_iter;
  InfcExploreRequest* request;
  guint current;
  guint total;

  /* TODO: This could later also show synchronization progress */

  gtk_tree_model_get(
    model,
    iter,
    INF_GTK_BROWSER_MODEL_COL_BROWSER, &browser,
    -1
  );

  if(browser != NULL)
  {
    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_NODE, &browser_iter,
      -1
    );

    if(infc_browser_iter_is_subdirectory(browser, browser_iter))
    {
      request = infc_browser_iter_get_explore_request(browser, browser_iter);
      if(request != NULL)
      {
        if(infc_explore_request_get_finished(request) == FALSE)
        {
          if(infc_explore_request_get_initiated(request) == FALSE)
          {
            current = 0;
            total = 1;
          }
          else
          {
            g_object_get(
              G_OBJECT(request),
              "current", &current,
              "total", &total,
              NULL
            );
          }
          
          g_object_set(
            G_OBJECT(renderer),
            "visible", TRUE,
            "value", current * 100 / total,
            "text", "Exploring...",
            NULL
          );

          return;
        }
      }
    }

    g_object_unref(G_OBJECT(browser));
  }
  
  g_object_set(
    G_OBJECT(renderer),
    "visible", FALSE,
    NULL
  );
}

static void
inf_gtk_browser_view_status_data_func(GtkTreeViewColumn* column,
                                      GtkCellRenderer* renderer,
                                      GtkTreeModel* model,
                                      GtkTreeIter* iter,
                                      gpointer user_data)
{
  GtkTreeIter iter_parent;
  InfGtkBrowserModelStatus status;
  GError* error;

  if(gtk_tree_model_iter_parent(model, &iter_parent, iter))
  {
    /* Status is currrently only shown for toplevel items */
    g_object_set(G_OBJECT(renderer), "visible", FALSE, NULL);
  }
  else
  {
    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_STATUS, &status,
      INF_GTK_BROWSER_MODEL_COL_ERROR, &error,
      -1
    );

    switch(status)
    {
    case INF_GTK_BROWSER_MODEL_DISCOVERED:
      g_object_set(
        G_OBJECT(renderer),
        "text", "Not connected",
        "foreground", "black",
        "visible", FALSE, /* Don't show */
        NULL
      );

      break;
    case INF_GTK_BROWSER_MODEL_RESOLVING:
    case INF_GTK_BROWSER_MODEL_CONNECTING:
      g_object_set(
        G_OBJECT(renderer),
        "text", "Connecting...",
        "foreground", "black",
        "visible", TRUE,
        NULL
      );

      break;
    case INF_GTK_BROWSER_MODEL_CONNECTED:
      g_object_set(
        G_OBJECT(renderer),
        "text", "Connected",
        "foreground", "black",
        "visible", FALSE, /* Don't show */
        NULL
      );

      break;
    case INF_GTK_BROWSER_MODEL_ERROR:
      g_assert(error != NULL);

      g_object_set(
        G_OBJECT(renderer),
        "text", error->message,
        "foreground", "#db1515",
        "visible", TRUE,
        NULL
      );

      break;
    default:
      g_assert_not_reached();
      break;
    }
  }
}

static void
inf_gtk_browser_view_init(GTypeInstance* instance,
                          gpointer g_class)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;

  view = INF_GTK_BROWSER_VIEW(instance);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  priv->treeview = gtk_tree_view_new();
  priv->column = gtk_tree_view_column_new();
  
  priv->renderer_icon = gtk_cell_renderer_pixbuf_new();
  priv->renderer_status_icon = gtk_cell_renderer_pixbuf_new();
  priv->renderer_name = gtk_cell_renderer_text_new();
  priv->renderer_progress = gtk_cell_renderer_progress_new();
  priv->renderer_status = gtk_cell_renderer_text_new();

  priv->browsers =
    g_array_new(FALSE, FALSE, sizeof(InfGtkBrowserViewObject));
  priv->explore_requests =
    g_array_new(FALSE, FALSE, sizeof(InfGtkBrowserViewObject));

  g_object_set(G_OBJECT(priv->renderer_status), "xpad", 10, NULL);
  g_object_set(G_OBJECT(priv->renderer_status_icon), "xpad", 5, NULL);

  gtk_tree_view_column_pack_start(priv->column, priv->renderer_icon, FALSE);

  gtk_tree_view_column_pack_start(
    priv->column,
    priv->renderer_status_icon,
    FALSE
  );

  gtk_tree_view_column_pack_start(priv->column, priv->renderer_name, FALSE);
  gtk_tree_view_column_pack_start(
    priv->column,
    priv->renderer_progress,
    FALSE
  );

  gtk_tree_view_column_pack_start(priv->column, priv->renderer_status, TRUE);

  gtk_tree_view_column_set_cell_data_func(
    priv->column,
    priv->renderer_icon,
    inf_gtk_browser_view_icon_data_func,
    NULL,
    NULL
  );

  gtk_tree_view_column_set_cell_data_func(
    priv->column,
    priv->renderer_status_icon,
    inf_gtk_browser_view_status_icon_data_func,
    NULL,
    NULL
  );

  gtk_tree_view_column_set_cell_data_func(
    priv->column,
    priv->renderer_name,
    inf_gtk_browser_view_name_data_func,
    NULL,
    NULL
  );

  gtk_tree_view_column_set_cell_data_func(
    priv->column,
    priv->renderer_progress,
    inf_gtk_browser_view_progress_data_func,
    NULL,
    NULL
  );

  gtk_tree_view_column_set_cell_data_func(
    priv->column,
    priv->renderer_status,
    inf_gtk_browser_view_status_data_func,
    NULL,
    NULL
  );

  g_signal_connect(
    GTK_TREE_VIEW(priv->treeview),
    "row-expanded",
    G_CALLBACK(inf_gtk_browser_view_row_expanded_cb),
    view
  );

  g_signal_connect(
    GTK_TREE_VIEW(priv->treeview),
    "row-activated",
    G_CALLBACK(inf_gtk_browser_view_row_activated_cb),
    view
  );

  gtk_tree_view_append_column(GTK_TREE_VIEW(priv->treeview), priv->column);
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(priv->treeview), FALSE);
  gtk_container_add(GTK_CONTAINER(view), priv->treeview);
  gtk_widget_show(priv->treeview);
}

static void
inf_gtk_browser_view_dispose(GObject* object)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;

  view = INF_GTK_BROWSER_VIEW(object);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  if(priv->treeview != NULL)
  {
    inf_gtk_browser_view_set_model(view, NULL);
    priv->treeview = NULL;
  }

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_gtk_browser_view_finalize(GObject* object)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;

  view = INF_GTK_BROWSER_VIEW(object);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  g_assert(priv->browsers->len == 0);
  g_assert(priv->explore_requests->len == 0);

  g_array_free(priv->browsers, TRUE);
  g_array_free(priv->explore_requests, TRUE);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_gtk_browser_view_set_property(GObject* object,
                                  guint prop_id,
                                  const GValue* value,
                                  GParamSpec* pspec)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;

  view = INF_GTK_BROWSER_VIEW(object);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  switch(prop_id)
  {
  case PROP_MODEL:
    inf_gtk_browser_view_set_model(
      view,
      INF_GTK_BROWSER_MODEL(g_value_get_object(value))
    );
  
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_gtk_browser_view_get_property(GObject* object,
                                  guint prop_id,
                                  GValue* value,
                                  GParamSpec* pspec)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;

  view = INF_GTK_BROWSER_VIEW(object);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  switch(prop_id)
  {
  case PROP_MODEL:
    g_value_set_object(
      value,
      G_OBJECT(gtk_tree_view_get_model(GTK_TREE_VIEW(priv->treeview)))
    );

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_gtk_browser_view_destroy(GtkObject* object)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;

  view = INF_GTK_BROWSER_VIEW(object);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  if(priv->treeview != NULL)
  {
    /* Unset model while treeview is alive */
    inf_gtk_browser_view_set_model(view, NULL);
    priv->treeview = NULL;
  }

  if(GTK_OBJECT_CLASS(parent_class)->destroy)
    GTK_OBJECT_CLASS(parent_class)->destroy(object);
}

static void
inf_gtk_browser_view_size_request(GtkWidget* widget,
                                  GtkRequisition* requisition)
{
  InfGtkBrowserViewPrivate* priv;
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(widget);

  if(priv->treeview != NULL)
  {
    gtk_widget_size_request(priv->treeview, requisition);
  }
  else
  {
    requisition->width = 0;
    requisition->height = 0;
  }
}

static void
inf_gtk_browser_view_size_allocate(GtkWidget* widget,
                                   GtkAllocation* allocation)
{
  InfGtkBrowserViewPrivate* priv;
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(widget);

  if(priv->treeview != NULL)
  {
    gtk_widget_size_allocate(priv->treeview, allocation);
  }
  else
  {
    allocation->x = 0;
    allocation->y = 0;
    allocation->width = 0;
    allocation->height = 0;
  }
}

static void
inf_gtk_browser_view_set_scroll_adjustments(InfGtkBrowserView* view,
                                            GtkAdjustment* hadj,
                                            GtkAdjustment* vadj)
{
  InfGtkBrowserViewPrivate* priv;
  GtkWidgetClass* klass;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  if(priv->treeview != NULL)
  {
    klass = GTK_WIDGET_GET_CLASS(priv->treeview);

    /* Delegate to TreeView */
    g_assert(klass->set_scroll_adjustments_signal);
    g_signal_emit(
      G_OBJECT(priv->treeview),
      klass->set_scroll_adjustments_signal,
      0,
      hadj,
      vadj
    );
  }
}

/*
 * GType registration
 */

static void
inf_gtk_browser_view_class_init(gpointer g_class,
                                gpointer class_data)
{
  GObjectClass* object_class;
  GtkObjectClass* gtk_object_class;
  GtkWidgetClass* widget_class;
  InfGtkBrowserViewClass* view_class;

  object_class = G_OBJECT_CLASS(g_class);
  gtk_object_class = GTK_OBJECT_CLASS(g_class);
  widget_class = GTK_WIDGET_CLASS(g_class);
  view_class = INF_GTK_BROWSER_VIEW_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfGtkBrowserViewPrivate));

  object_class->dispose = inf_gtk_browser_view_dispose;
  object_class->finalize = inf_gtk_browser_view_finalize;
  object_class->set_property = inf_gtk_browser_view_set_property;
  object_class->get_property = inf_gtk_browser_view_get_property;
  gtk_object_class->destroy = inf_gtk_browser_view_destroy;
  widget_class->size_request = inf_gtk_browser_view_size_request;
  widget_class->size_allocate = inf_gtk_browser_view_size_allocate;

  view_class->set_scroll_adjustments =
    inf_gtk_browser_view_set_scroll_adjustments;

  g_object_class_install_property(
    object_class,
    PROP_MODEL,
    g_param_spec_object(
      "model",
      "Model", 
      "The model to display",
      INF_GTK_TYPE_BROWSER_MODEL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    )
  );

  widget_class->set_scroll_adjustments_signal = g_signal_new(
    "set-scroll-adjustments",
    G_TYPE_FROM_CLASS(object_class),
    G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
    G_STRUCT_OFFSET(InfGtkBrowserViewClass, set_scroll_adjustments),
    NULL, NULL,
    inf_marshal_VOID__OBJECT_OBJECT,
    G_TYPE_NONE,
    2,
    GTK_TYPE_ADJUSTMENT,
    GTK_TYPE_ADJUSTMENT
  );
}

GType
inf_gtk_browser_view_get_type(void)
{
  static GType browser_view_type = 0;

  if(!browser_view_type)
  {
    static const GTypeInfo browser_view_type_info = {
      sizeof(InfGtkBrowserViewClass),    /* class_size */
      NULL,                              /* base_init */
      NULL,                              /* base_finalize */
      inf_gtk_browser_view_class_init,   /* class_init */
      NULL,                              /* class_finalize */
      NULL,                              /* class_data */
      sizeof(InfGtkBrowserView),         /* instance_size */
      0,                                 /* n_preallocs */
      inf_gtk_browser_view_init,         /* instance_init */
      NULL                               /* value_table */
    };

    browser_view_type = g_type_register_static(
      GTK_TYPE_BIN,
      "InfGtkBrowserView",
      &browser_view_type_info,
      0
    );
  }

  return browser_view_type;
}

/*
 * Public API.
 */

/** inf_gtk_browser_view_new:
 *
 * Creates a new #InfGtkBrowserView.
 *
 * Return Value: A new #InfGtkBrowserView.
 **/
GtkWidget*
inf_gtk_browser_view_new(void)
{
  GObject* object;
  object = g_object_new(INF_GTK_TYPE_BROWSER_VIEW, NULL);
  return GTK_WIDGET(object);
}

/** inf_gtk_browser_view_new_with_model:
 *
 * @model: A #InfGtkBrowserModel.
 *
 * Creates a new #InfGtkBrowserView showing @model.
 *
 * Return Value: A new #InfGtkBrowserView.
 **/
GtkWidget*
inf_gtk_browser_view_new_with_model(InfGtkBrowserModel* model)
{
  GObject* object;
  object = g_object_new(INF_GTK_TYPE_BROWSER_VIEW, "model", model, NULL);
  return GTK_WIDGET(object);
}

/* vim:set et sw=2 ts=2: */