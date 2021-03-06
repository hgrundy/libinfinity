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

/**
 * SECTION:inf-gtk-certificate-dialog
 * @title: InfGtkCertificateDialog
 * @short_description: A dialog warning the user about a server's certificate
 * @include: libinfgtk/inf-gtk-certificate-dialog.h
 * @stability: Unstable
 *
 * #InfGtkCertificateDialog is a dialog that can be shown to a user if the
 * validation of the server's certificate cannot be performed automatically.
 * The dialog will present to the user the reason(s) of the validation
 * failure and might ask whether to fully establish the connection to the
 * server or not.
 **/

#include <libinfgtk/inf-gtk-certificate-dialog.h>
#include <libinfgtk/inf-gtk-certificate-view.h>
#include <libinfinity/common/inf-cert-util.h>
#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-define-enum.h>

#include <gnutls/x509.h>
#include <time.h>

typedef struct _InfGtkCertificateDialogPrivate InfGtkCertificateDialogPrivate;
struct _InfGtkCertificateDialogPrivate {
  InfCertificateChain* certificate_chain;
  gnutls_x509_crt_t pinned_certificate;
  InfCertificateVerifyFlags verify_flags;
  gchar* hostname;

  GtkTreeStore* certificate_store;

  GtkWidget* caption;
  GtkWidget* info;
  GtkWidget* certificate_expander;
  GtkWidget* certificate_tree_view;
  GtkWidget* certificate_info_view;
  GtkCellRenderer* text_renderer;
};

enum {
  PROP_0,

  PROP_CERTIFICATE_CHAIN,
  PROP_PINNED_CERTIFICATE,
  PROP_VERIFY_FLAGS,
  PROP_HOSTNAME
};

#define INF_GTK_CERTIFICATE_DIALOG_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_GTK_TYPE_CERTIFICATE_DIALOG, InfGtkCertificateDialogPrivate))

G_DEFINE_TYPE_WITH_CODE(InfGtkCertificateDialog, inf_gtk_certificate_dialog, GTK_TYPE_DIALOG,
  G_ADD_PRIVATE(InfGtkCertificateDialog))

/* When a host presents a certificate different from one that we have pinned,
 * usually we warn the user that something fishy is going on. However, if the
 * pinned certificate has expired or will expire soon, then we kind of expect
 * the certificate to change, and issue a less "flashy" warning message. This
 * value defines how long before the pinned certificate expires we show a
 * less dramatic warning message. */
static const unsigned int
INF_GTK_CERTIFICATE_DIALOG_EXPIRATION_TOLERANCE = 30 * 24 * 3600; /* 30 days */

static void
inf_gtk_certificate_dialog_renew_info(InfGtkCertificateDialog* dialog)
{
  InfGtkCertificateDialogPrivate* priv;
  gnutls_x509_crt_t own_cert;
  time_t expiration_time;

  gint normal_width_chars;
  gint size;
  PangoFontDescription* font_desc;

  const gchar* ctext;
  gchar* text;
  gchar* markup;
  GString* info_text;
  GtkWidget* caption;
  GtkWidget* info;

  priv = INF_GTK_CERTIFICATE_DIALOG_PRIVATE(dialog);

  if(priv->verify_flags != 0 && priv->hostname != NULL)
  {
    own_cert =
      inf_certificate_chain_get_own_certificate(priv->certificate_chain);

    text = g_strdup_printf(
      _("The connection to host \"%s\" is not considered secure"),
      priv->hostname
    );

    gtk_label_set_text(GTK_LABEL(priv->caption), text);
    g_free(text);

    info_text = g_string_sized_new(256);

    if(priv->verify_flags & INF_CERTIFICATE_VERIFY_NOT_PINNED)
    {
      /* TODO: Here it might also be interesting to show the pinned
       * certificate to the user... */

      expiration_time = gnutls_x509_crt_get_expiration_time(
        priv->pinned_certificate
      );

      if(expiration_time != (time_t)(-1) &&
         time(NULL) > expiration_time - INF_GTK_CERTIFICATE_DIALOG_EXPIRATION_TOLERANCE)
      {
        ctext = _("The host has presented a new certificate.");
        markup = g_markup_printf_escaped("<b>%s</b>", ctext);

        g_string_append(info_text, markup);
        g_free(markup);
        g_string_append_c(info_text, ' ');

        g_string_append(
          info_text,
          _("Its previous certificate has expired or is closed to "
            "expiration. Please make sure that you trust the new "
            "certificate.")
        );
      }
      else
      {
        ctext = _("The host has presented an unexpected certificate!");
        markup = g_markup_printf_escaped("<b>%s</b>", ctext);

        g_string_append(info_text, markup);
        g_free(markup);
        g_string_append_c(info_text, ' ');

        g_string_append(
          info_text,
          _("This means someone might be eavesdropping on the connection. "
            "Please only continue if you expected this message, otherwise "
            "please contact the server administrator.")
        );
      }
    }
    else
    {
      g_string_append(
        info_text,
        _("The server certificate cannot be verified automatically. Please "
          "make sure that you trust this host before proceeding.")
      );

      if(priv->verify_flags & INF_CERTIFICATE_VERIFY_ISSUER_NOT_KNOWN)
      {
        if(info_text->len > 0)
          g_string_append(info_text, "\n\n");

        g_string_append(
          info_text,
          _("The issuer of the certificate is not known.")
        );
      }

      if(priv->verify_flags & INF_CERTIFICATE_VERIFY_HOSTNAME_MISMATCH)
      {
        if(info_text->len > 0)
          g_string_append(info_text, "\n\n");

        text = inf_cert_util_get_hostname(own_cert);

        g_string_append_printf(
          info_text,
          _("The hostname of the server, \"%s\", does not match the hostname "
            "the certificate is issued to, \"%s\"."),
          priv->hostname,
          text
        );

        g_free(text);
      }
    }

    gtk_label_set_markup(GTK_LABEL(priv->info), info_text->str);
    g_string_free(info_text, TRUE);
  }
  else
  {
    gtk_label_set_text(GTK_LABEL(priv->caption), NULL);
    gtk_label_set_text(GTK_LABEL(priv->info), NULL);
  }
}

static void
inf_gtk_certificate_dialog_set_chain(InfGtkCertificateDialog* dialog,
                                     InfCertificateChain* chain)
{
  InfGtkCertificateDialogPrivate* priv;
  guint i;
  gnutls_x509_crt_t crt;
  GtkTreeIter prev_row;
  GtkTreeIter new_row;
  GtkTreeIter* parent;
  GtkTreePath* path;

  priv = INF_GTK_CERTIFICATE_DIALOG_PRIVATE(dialog);

  if(priv->certificate_chain != NULL)
    inf_certificate_chain_unref(priv->certificate_chain);

  priv->certificate_chain = chain;

  gtk_tree_store_clear(priv->certificate_store);
  inf_gtk_certificate_view_set_certificate(
    INF_GTK_CERTIFICATE_VIEW(priv->certificate_info_view),
    NULL
  );

  parent = NULL;
  if(chain != NULL)
  {
    inf_certificate_chain_ref(chain);

    for(i = inf_certificate_chain_get_n_certificates(chain); i > 0; -- i)
    {
      crt = inf_certificate_chain_get_nth_certificate(chain, i - 1);
      gtk_tree_store_append(priv->certificate_store, &new_row, parent);
      gtk_tree_store_set(priv->certificate_store, &new_row, 0, crt, -1);

      prev_row = new_row;
      parent = &prev_row;
    }

    path = gtk_tree_model_get_path(
      GTK_TREE_MODEL(priv->certificate_store),
      &new_row
    );

    gtk_tree_view_expand_to_path(
      GTK_TREE_VIEW(priv->certificate_tree_view),
      path
    );

    gtk_tree_selection_select_path(
      gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->certificate_tree_view)),
      path
    );

    gtk_tree_view_scroll_to_cell(
      GTK_TREE_VIEW(priv->certificate_tree_view),
      path,
      NULL,
      FALSE,
      0.0,
      0.0
    );

    gtk_tree_path_free(path);
    gtk_widget_show(priv->certificate_expander);
  }
  else
  {
    gtk_widget_hide(priv->certificate_expander);
  }

  g_object_notify(G_OBJECT(dialog), "certificate-chain");
}

static void
inf_gtk_certificate_dialog_selection_changed_cb(GtkTreeSelection* selection,
                                                gpointer user_data)
{
  InfGtkCertificateDialog* dialog;
  InfGtkCertificateDialogPrivate* priv;
  GtkTreeIter iter;
  gnutls_x509_crt_t cert;

  dialog = INF_GTK_CERTIFICATE_DIALOG(user_data);
  priv = INF_GTK_CERTIFICATE_DIALOG_PRIVATE(dialog);

  if(gtk_tree_selection_get_selected(selection, NULL, &iter))
  {
    gtk_tree_model_get(
      GTK_TREE_MODEL(priv->certificate_store),
      &iter,
      0, &cert,
      -1
    );

    inf_gtk_certificate_view_set_certificate(
      INF_GTK_CERTIFICATE_VIEW(priv->certificate_info_view),
      cert
    );
  }
  else
  {
    inf_gtk_certificate_view_set_certificate(
      INF_GTK_CERTIFICATE_VIEW(priv->certificate_info_view),
      NULL
    );
  }
}

static void
inf_gtk_certificate_dialog_chain_data_func(GtkTreeViewColumn* column,
                                           GtkCellRenderer* renderer,
                                           GtkTreeModel* tree_model,
                                           GtkTreeIter* iter,
                                           gpointer user_data)
{
  gpointer crt_ptr;
  gnutls_x509_crt_t cert;
  GValue value = { 0 };
  gchar* common_name;

  gtk_tree_model_get(tree_model, iter, 0, &crt_ptr, -1);
  cert = (gnutls_x509_crt_t)crt_ptr;

  common_name =
    inf_cert_util_get_dn_by_oid(cert, GNUTLS_OID_X520_COMMON_NAME, 0);

  g_value_init(&value, G_TYPE_STRING);

  if(common_name != NULL)
    g_value_take_string(&value, common_name);
  else
    g_value_set_static_string(&value, _("<Unknown Certificate Holder>"));

  g_object_set_property(G_OBJECT(renderer), "text", &value);
  g_value_unset(&value);
}

static void
inf_gtk_certificate_dialog_init(InfGtkCertificateDialog* dialog)
{
  InfGtkCertificateDialogPrivate* priv;
  priv = INF_GTK_CERTIFICATE_DIALOG_PRIVATE(dialog);

  priv->certificate_chain = NULL;
  priv->pinned_certificate = NULL;
  priv->verify_flags = 0;
  priv->hostname = NULL;

  gtk_widget_init_template(GTK_WIDGET(dialog));

  gtk_tree_selection_set_mode(
    gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->certificate_tree_view)),
    GTK_SELECTION_BROWSE
  );

  gtk_tree_view_column_set_cell_data_func(
    gtk_tree_view_get_column(GTK_TREE_VIEW(priv->certificate_tree_view), 0),
    priv->text_renderer,
    inf_gtk_certificate_dialog_chain_data_func,
    NULL,
    NULL
  );
}

static void
inf_gtk_certificate_dialog_finalize(GObject* object)
{
  InfGtkCertificateDialog* dialog;
  InfGtkCertificateDialogPrivate* priv;

  dialog = INF_GTK_CERTIFICATE_DIALOG(object);
  priv = INF_GTK_CERTIFICATE_DIALOG_PRIVATE(dialog);

  inf_certificate_chain_unref(priv->certificate_chain);
  g_free(priv->hostname);

  G_OBJECT_CLASS(inf_gtk_certificate_dialog_parent_class)->finalize(object);
}

static void
inf_gtk_certificate_dialog_set_property(GObject* object,
                                        guint prop_id,
                                        const GValue* value,
                                        GParamSpec* pspec)
{
  InfGtkCertificateDialog* dialog;
  InfGtkCertificateDialogPrivate* priv;

  dialog = INF_GTK_CERTIFICATE_DIALOG(object);
  priv = INF_GTK_CERTIFICATE_DIALOG_PRIVATE(dialog);

  switch(prop_id)
  {
  case PROP_CERTIFICATE_CHAIN:
    inf_gtk_certificate_dialog_set_chain(
      dialog,
      (InfCertificateChain*)g_value_get_boxed(value)
    );

    break;
  case PROP_PINNED_CERTIFICATE:
    priv->pinned_certificate = g_value_get_pointer(value);
    inf_gtk_certificate_dialog_renew_info(dialog);
    break;
  case PROP_VERIFY_FLAGS:
    priv->verify_flags = g_value_get_flags(value);

    if(priv->verify_flags != 0 && priv->hostname != NULL)
      inf_gtk_certificate_dialog_renew_info(dialog);

    break;
  case PROP_HOSTNAME:
    if(priv->hostname != NULL) g_free(priv->hostname);
    priv->hostname = g_value_dup_string(value);
    if(priv->verify_flags != 0 && priv->hostname != NULL)
      inf_gtk_certificate_dialog_renew_info(dialog);

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_gtk_certificate_dialog_get_property(GObject* object,
                                        guint prop_id,
                                        GValue* value,
                                        GParamSpec* pspec)
{
  InfGtkCertificateDialog* dialog;
  InfGtkCertificateDialogPrivate* priv;

  dialog = INF_GTK_CERTIFICATE_DIALOG(object);
  priv = INF_GTK_CERTIFICATE_DIALOG_PRIVATE(dialog);

  switch(prop_id)
  {
  case PROP_CERTIFICATE_CHAIN:
    g_value_set_boxed(value, priv->certificate_chain);
    break;
  case PROP_PINNED_CERTIFICATE:
    g_value_set_pointer(value, priv->pinned_certificate);
    break;
  case PROP_VERIFY_FLAGS:
    g_value_set_flags(value, priv->verify_flags);
    break;
  case PROP_HOSTNAME:
    g_value_set_string(value, priv->hostname);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/*
 * GType registration
 */

static void
inf_gtk_certificate_dialog_class_init(
  InfGtkCertificateDialogClass* certificate_dialog_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(certificate_dialog_class);

  object_class->finalize = inf_gtk_certificate_dialog_finalize;
  object_class->set_property = inf_gtk_certificate_dialog_set_property;
  object_class->get_property = inf_gtk_certificate_dialog_get_property;

  gtk_widget_class_set_template_from_resource(
    GTK_WIDGET_CLASS(object_class),
    "/de/0x539/libinfgtk/ui/infgtkcertificatedialog.ui"
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(object_class),
    InfGtkCertificateDialog,
    certificate_store
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(object_class),
    InfGtkCertificateDialog,
    caption
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(object_class),
    InfGtkCertificateDialog,
    info
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(object_class),
    InfGtkCertificateDialog,
    certificate_expander
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(object_class),
    InfGtkCertificateDialog,
    certificate_tree_view
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(object_class),
    InfGtkCertificateDialog,
    certificate_info_view
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(object_class),
    InfGtkCertificateDialog,
    text_renderer
  );

  gtk_widget_class_bind_template_callback(
    GTK_WIDGET_CLASS(object_class),
    inf_gtk_certificate_dialog_selection_changed_cb
  );

  g_object_class_install_property(
    object_class,
    PROP_CERTIFICATE_CHAIN,
    g_param_spec_boxed(
      "certificate-chain",
      "Certificate chain",
      "The certificate chain to show in the dialog",
      INF_TYPE_CERTIFICATE_CHAIN,
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_PINNED_CERTIFICATE,
    g_param_spec_pointer(
      "pinned-certificate",
      "Pinned Certificate",
      "The certificate that we had pinned for this host",
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_VERIFY_FLAGS,
    g_param_spec_flags(
      "verify-flags",
      "Verify flags",
      "What warnings about the certificate to display",
      INF_TYPE_CERTIFICATE_VERIFY_FLAGS,
      0,
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_HOSTNAME,
    g_param_spec_string(
      "hostname",
      "Host name",
      "Host name of the server from which the certificate is",
      NULL,
      G_PARAM_READWRITE
    )
  );
}

/*
 * Public API.
 */

/**
 * inf_gtk_certificate_dialog_new: (constructor)
 * @parent: Parent #GtkWindow of the dialog.
 * @dialog_flags: Flags for the dialog, see #GtkDialogFlags.
 * @verify_flags: What certificate warnings to show, see
 * #InfCertificateVerifyFlags.
 * @hostname: The host name of the server that provides the certificate.
 * @certificate_chain: (transfer none): The certificate chain provided by
 * the server.
 * @pinned_certificate: (transfer none): The certificate that we had pinned
 * for this host, or %NULL.
 *
 * Creates a new #InfGtkCertificateDialog. A #InfGtkCertificateDialog shows
 * a warning about a server's certificate to a user, for example when the
 * issuer is not trusted or the hostname does not match what the certificate
 * was issued to.
 *
 * Returns: (transfer full): A new #InfGtkCertificateDialog.
 */
InfGtkCertificateDialog*
inf_gtk_certificate_dialog_new(GtkWindow* parent,
                               GtkDialogFlags dialog_flags,
                               InfCertificateVerifyFlags verify_flags,
                               const gchar* hostname,
                               InfCertificateChain* certificate_chain,
                               gnutls_x509_crt_t pinned_certificate)
{
  GObject* object;

  g_return_val_if_fail(parent == NULL || GTK_IS_WINDOW(parent), NULL);
  g_return_val_if_fail(verify_flags != 0, NULL);
  g_return_val_if_fail(hostname != NULL, NULL);
  g_return_val_if_fail(certificate_chain != NULL, NULL);

  object = g_object_new(
    INF_GTK_TYPE_CERTIFICATE_DIALOG,
    "certificate-chain", certificate_chain,
    "pinned-certificate", pinned_certificate,
    "verify-flags", verify_flags,
    "hostname", hostname,
    NULL
  );

  if(dialog_flags & GTK_DIALOG_MODAL)
    gtk_window_set_modal(GTK_WINDOW(object), TRUE);

  if(dialog_flags & GTK_DIALOG_DESTROY_WITH_PARENT)
    gtk_window_set_destroy_with_parent(GTK_WINDOW(object), TRUE);

  gtk_window_set_transient_for(GTK_WINDOW(object), parent);
  return INF_GTK_CERTIFICATE_DIALOG(object);
}

/* vim:set et sw=2 ts=2: */
