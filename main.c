#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>

#include <my_global.h>
#include <mysql.h>

#include <gtk/gtk.h>

#define BUFFER_SIZE 65536

typedef struct {
    char query_string[BUFFER_SIZE];
    MYSQL* con;
} query_data;

GtkWidget* result_window;
GtkWidget* result_tabs;
GtkWidget* log_text;
int query_count = 1;

gboolean log_text_append( gpointer data )
{
    GtkTextIter iter;
    GtkTextBuffer* text_buffer = gtk_text_view_get_buffer( GTK_TEXT_VIEW( log_text ) );
    gtk_text_buffer_get_end_iter( text_buffer, &iter ); 
    gtk_text_buffer_insert( text_buffer, &iter, (const char*)data, -1 );

    free( data );

    return FALSE;
}

void print_message( const char* str, ... )
{
    char* buffer = malloc( sizeof(char)*1024 );
    va_list args;

    va_start( args, str );
    vsprintf( buffer, str, args );
    va_end( args );

    gdk_threads_add_idle( log_text_append, (gpointer)buffer ); 
}

void print_error( MYSQL* con )
{
    print_message( "MySQL Error: %s\n", mysql_error(con) );
}

GtkWidget* create_query_result_grid( MYSQL_RES* result )
{
    GType list_types[256]; //If your query returns more than 256 columns you're screwed
    int col_index[256];
    GValue col_values[256] = { G_VALUE_INIT };
    GtkListStore* list_store;
	GtkTreeModel* sortmodel;
	GtkWidget* listwidget;
	GtkTreeViewColumn *col;
	GtkCellRenderer *renderer;
	GtkTreeIter iter;
    GtkWidget* label;

    MYSQL_FIELD* fields;
    MYSQL_ROW row;

    int num_fields = mysql_num_fields( result );
    fields = mysql_fetch_fields( result );
    int i;

    for( i = 0; i< num_fields; i++ )
    {
        list_types[i] = G_TYPE_STRING;
        col_index[i] = i;
        g_value_init( &col_values[i], G_TYPE_STRING );
    }

    //Create the list store and store the query results, it is waaayyyy faster to do this before
    //creating the widgets and attaching the list to them.
	list_store=gtk_list_store_newv( num_fields, list_types );
    while( row = mysql_fetch_row( result ) )
    {
        gtk_list_store_append( list_store, &iter );
        for( i = 0; i< num_fields; i++ )
        {
            g_value_set_string( &col_values[i], row[i] ? row[i] : "NULL" );
        }
        gtk_list_store_set_valuesv( list_store, &iter, col_index, col_values, num_fields );
    }

    //Create the widgets
	sortmodel=gtk_tree_model_sort_new_with_model(GTK_TREE_MODEL(list_store));
	listwidget=gtk_tree_view_new_with_model(sortmodel);

    //Create column headings inside grid
    for( i = 0; i < num_fields; i++ )
    {
        col= gtk_tree_view_column_new_with_attributes( fields[i].name, 
               gtk_cell_renderer_text_new(),
               "text", i,
               NULL );
        
        //Have to create a label and set it so that underlines are displayed in the headers
        label = gtk_label_new( fields[i].name );
        gtk_label_set_use_underline( GTK_LABEL(label), FALSE );
        gtk_tree_view_column_set_widget( col, label );
        gtk_widget_show( label );

        gtk_tree_view_append_column( GTK_TREE_VIEW(listwidget), col );
        gtk_tree_view_column_set_resizable( col, TRUE );
        //gtk_tree_view_column_set_sort_column_id( col,i );
        //gtk_tree_view_column_pack_start( col, renderer, TRUE );
    }

	GtkWidget* scrollarea=gtk_scrolled_window_new(NULL,NULL);
	gtk_container_add(GTK_CONTAINER(scrollarea),listwidget);

	gtk_tree_view_columns_autosize(GTK_TREE_VIEW(listwidget));
    return scrollarea;
}

//Adds another tab to the GtkNotebook widget that stores our query result grids
void create_query_result_tab( MYSQL_RES* result )
{
    char tab_title[64];
    sprintf( tab_title, "Result %i", query_count++ );
    GtkWidget* tab_contents = create_query_result_grid( result );
    gtk_notebook_append_page( GTK_NOTEBOOK(result_tabs), tab_contents, gtk_label_new( tab_title ) );
    gtk_widget_show_all( tab_contents );
}

gboolean window_key_press(GtkWidget* widget,GdkEvent* event,gpointer user)
{
	GdkEventKey* keyevent=&event->key;
	if(keyevent->keyval==GDK_KEY_Escape) gtk_main_quit();
}

//Create a gtk window to hold our query output
void create_query_window()
{
    result_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(result_window),"Query Result!");
        
    //TODO: Fix this size
    gtk_widget_set_size_request(result_window,960,640);
    gtk_window_set_resizable( GTK_WINDOW(result_window), TRUE );

    gtk_widget_show_all(result_window);

    GtkTextIter iter;
    GtkTextBuffer* text_buffer;

    result_tabs = gtk_notebook_new();
    gtk_notebook_set_show_tabs( GTK_NOTEBOOK( result_tabs ), TRUE );
	gtk_widget_set_hexpand( result_tabs, TRUE );
	gtk_widget_set_vexpand( result_tabs, TRUE );

    log_text = gtk_text_view_new();
    gtk_widget_set_size_request( log_text, -1, 192 );
    gtk_text_view_set_cursor_visible( GTK_TEXT_VIEW( log_text ), FALSE );

    //Split the window into two pieces
    GtkPaned* pane = GTK_PANED( gtk_paned_new( GTK_ORIENTATION_VERTICAL ) );
    gtk_paned_add1( pane, result_tabs ); 
    gtk_paned_add2( pane, log_text ); 
    gtk_paned_set_position( pane, 500 );

    gtk_container_add( GTK_CONTAINER( result_window ), GTK_WIDGET( pane ) );

    //Hook into events
    g_signal_connect(G_OBJECT(result_window), "key-press-event", G_CALLBACK(window_key_press), NULL);
    g_signal_connect(G_OBJECT(result_window), "delete-event", gtk_main_quit, NULL);
            
    gtk_widget_show_all(result_window);
}

gboolean fetch_query( gpointer result )
{
    create_query_result_tab( (MYSQL_RES*)result );

    return FALSE;
}

double time_diff( gboolean reset )
{
    static struct timeval tp;

    if( reset )
    {
        gettimeofday( &tp, NULL );
        return 0.0;
    }

    struct timeval tv;
    struct timeval dt;

    gettimeofday( &tv, NULL );

    timersub( &tv, &tp, &dt );

    tp.tv_sec = tv.tv_sec;
    tp.tv_usec = tv.tv_usec;

    return (double)dt.tv_sec + ((double)dt.tv_usec/1000000.0);
}

void run_query( gpointer data )
{
    query_data* sql_data = (query_data*)data;

    time_diff( TRUE );
    print_message( "Running Query: %s\n", sql_data->query_string );

    if( mysql_query( sql_data->con, sql_data->query_string ) )
    {
        print_error( sql_data->con );
        return;
    }

    MYSQL_RES* result = mysql_store_result( sql_data->con );
    if( result == NULL )
    {
        print_message( "Query Successful (%.4fs)\n", time_diff( FALSE ) );
        return;
    }

    print_message( "Query Successful, %i rows returned (%.4fs)\n", mysql_num_rows( result ), time_diff( FALSE ) );
    gdk_threads_add_idle( fetch_query, (gpointer)result );

    return;
}

gpointer execute_input( gpointer data )
{
    MYSQL* con = mysql_init( NULL );

    if( mysql_real_connect( con, "localhost", "root", "", NULL, 0, NULL, 0 ) == NULL )
    {
        print_error( con );
    }

    char* token = strtok( (char*)data, ";" );
    query_data sql_data;
    sql_data.con = con;
    while( token )
    {
        strcpy( sql_data.query_string, token );

        if( strlen( sql_data.query_string ) > 1 )
            run_query( (gpointer)&sql_data );

        token = strtok( NULL, ";" );
    }

    mysql_close( con );

    return NULL;
}

int main( int argc, char* argv[] )
{
    gtk_init( &argc, &argv );

    char buffer[BUFFER_SIZE];
    //Read stdin
    read(STDIN_FILENO, buffer, BUFFER_SIZE);

    create_query_window();

    g_thread_new( "sqlstuff", execute_input, (gpointer)buffer );

    gtk_main();
}
