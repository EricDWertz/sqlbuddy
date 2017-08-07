#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>

#include <my_global.h>
#include <mysql.h>

#include <gtk/gtk.h>

#define BUFFER_SIZE 65536
#define FIFO_LOCATION "/tmp/sqlbuddy"

#define MSG_NORMAL 0
#define MSG_ERROR 1
#define MSG_SUCCESS 2

typedef struct {
    char query_string[BUFFER_SIZE];
    MYSQL* con;
} query_data;

typedef struct {
    char* message;
    int type;
} log_message;

GtkWidget* result_window;
GtkWidget* result_tabs;
GtkWidget* log_text;
int query_count = 1;

GtkTextTag* text_normal;
GtkTextTag* text_success;
GtkTextTag* text_error;

int fifofd;
int readfifo = TRUE;

void quit_app( gpointer user )
{
    readfifo = FALSE; //Kill the thread

    //Unlink the fifo
    unlink( FIFO_LOCATION );

    gtk_main_quit();
}

gboolean log_text_append( gpointer data )
{
    log_message* msg = (log_message*)data;

    GtkTextTag* tag;
    switch( msg->type )
    {
        case MSG_NORMAL:
            tag = text_normal;
            break;
        case MSG_ERROR:
            tag = text_error;
            break;
        case MSG_SUCCESS:
            tag = text_success;
            break;
        default:
            tag = text_normal;
            break;
    }

    GtkTextIter iter;
    GtkTextBuffer* text_buffer = gtk_text_view_get_buffer( GTK_TEXT_VIEW( log_text ) );
    GtkTextMark* mark = gtk_text_buffer_get_insert( text_buffer );
    gtk_text_buffer_get_end_iter( text_buffer, &iter ); 
    gtk_text_buffer_move_mark( text_buffer, mark, &iter );
    gtk_text_buffer_insert_with_tags( text_buffer, &iter, msg->message, -1, tag, NULL );

    gtk_text_view_scroll_to_mark( GTK_TEXT_VIEW( log_text ), mark, 0.0, TRUE, 0.0, 0.0 );

    free( msg->message );
    free( msg );

    return FALSE;
}

void print_message( const char* str, int type, ... )
{
    log_message* msg = malloc( sizeof(log_message) );
    msg->message = malloc( sizeof(char)*BUFFER_SIZE );

    va_list args;

    va_start( args, type );
    vsprintf( msg->message, str, args );
    va_end( args );
    msg->type = type;

    gdk_threads_add_idle_full( G_PRIORITY_HIGH_IDLE, log_text_append, (gpointer)msg, NULL ); 
}

void print_error( MYSQL* con )
{
    print_message( "MySQL Error: %s\n", MSG_ERROR, mysql_error(con) );
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

	listwidget=gtk_tree_view_new();

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
    gtk_widget_show_all( scrollarea );

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
    gtk_tree_view_set_model( GTK_TREE_VIEW(listwidget), GTK_TREE_MODEL(list_store) );

	//gtk_tree_view_columns_autosize(GTK_TREE_VIEW(listwidget));
    return scrollarea;
}

//Adds another tab to the GtkNotebook widget that stores our query result grids
void create_query_result_tab( MYSQL_RES* result )
{
    char tab_title[64];
    int i;
    sprintf( tab_title, "Result %i", query_count++ );
    GtkWidget* tab_contents = create_query_result_grid( result );
    i = gtk_notebook_append_page( GTK_NOTEBOOK(result_tabs), tab_contents, gtk_label_new( tab_title ) );
    i = gtk_notebook_get_n_pages( GTK_NOTEBOOK(result_tabs) ) - i;

    while( i > 0 )
    {
        gtk_notebook_next_page( GTK_NOTEBOOK(result_tabs) );
        i--;
    }

    gtk_widget_show_all( tab_contents );
}

gboolean window_key_press(GtkWidget* widget,GdkEvent* event,gpointer user)
{
	GdkEventKey* keyevent=&event->key;
	if(keyevent->keyval==GDK_KEY_Escape) 
        quit_app( NULL );
}

//Init the various tags for formatting text in the log container
void init_text_tags()
{
    GtkTextBuffer* text_buffer = gtk_text_view_get_buffer( GTK_TEXT_VIEW( log_text ) );
    text_normal = gtk_text_buffer_create_tag( text_buffer, "normal", "foreground", "#000000", NULL );
    text_error = gtk_text_buffer_create_tag( text_buffer, "error", "foreground", "#CC0000", NULL );
    text_success = gtk_text_buffer_create_tag( text_buffer, "success", "foreground", "#007700", NULL );
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

    result_tabs = gtk_notebook_new();
    gtk_notebook_set_show_tabs( GTK_NOTEBOOK( result_tabs ), TRUE );
	gtk_widget_set_hexpand( result_tabs, TRUE );
	gtk_widget_set_vexpand( result_tabs, TRUE );

    log_text = gtk_text_view_new();
    gtk_widget_set_size_request( log_text, -1, 192 );
    gtk_text_view_set_cursor_visible( GTK_TEXT_VIEW( log_text ), FALSE );

    GtkWidget* scrollarea = gtk_scrolled_window_new( NULL, NULL );
    gtk_container_add( GTK_CONTAINER(scrollarea), log_text );

    //Split the window into two pieces
    GtkPaned* pane = GTK_PANED( gtk_paned_new( GTK_ORIENTATION_VERTICAL ) );
    gtk_paned_add1( pane, result_tabs ); 
    gtk_paned_add2( pane, scrollarea ); 
    gtk_paned_set_position( pane, 500 );

    gtk_container_add( GTK_CONTAINER( result_window ), GTK_WIDGET( pane ) );

    //Hook into events
    g_signal_connect(G_OBJECT(result_window), "key-press-event", G_CALLBACK( window_key_press ), NULL);
    g_signal_connect(G_OBJECT(result_window), "delete-event", G_CALLBACK( quit_app ), NULL);
            
    gtk_widget_show_all(result_window);

    init_text_tags();
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

gboolean run_query( gpointer data )
{
    query_data* sql_data = (query_data*)data;

    time_diff( TRUE );
    print_message( "Running Query: %s\n", MSG_NORMAL, sql_data->query_string );

    if( mysql_query( sql_data->con, sql_data->query_string ) )
    {
        print_error( sql_data->con );
        return FALSE;
    }

    MYSQL_RES* result = mysql_store_result( sql_data->con );
    if( result == NULL )
    {
        print_message( "Query Successful (%.4fs)\n", MSG_SUCCESS, time_diff( FALSE ) );
        return TRUE;
    }

    print_message( "Query Successful, %'i rows returned (%.4fs)\n", MSG_SUCCESS, mysql_num_rows( result ), time_diff( FALSE ) );
    gdk_threads_add_idle_full( G_PRIORITY_HIGH_IDLE, fetch_query, (gpointer)result, NULL );

    return TRUE;
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
            if( !run_query( (gpointer)&sql_data ) )
                break;

        token = strtok( NULL, ";" );
    }

    mysql_close( con );

    return NULL;
}

gpointer fifo_reader( gpointer data )
{
    char buffer[BUFFER_SIZE];
    int buffer_len;
    fifofd = open( FIFO_LOCATION, O_RDONLY );

    while( readfifo )
    {
        buffer_len = read(fifofd, buffer, BUFFER_SIZE);
        if( buffer_len > 0 )
        {
            printf( "Read from fifo: %s\n", buffer );
            g_thread_new( "sqlstuff", execute_input, (gpointer)buffer );
        }
    }

    close( fifofd );
}

//Returns true if app should exit
gboolean check_for_fifo( const char* buffer, int buffer_len )
{
    if( access( FIFO_LOCATION, W_OK ) != -1 )
    {
        //Fifo exists, we should write our buffer to it...
        fifofd = open( FIFO_LOCATION, O_WRONLY | O_NONBLOCK );
        if( fifofd == -1 )
        {
            printf( "Error opening the fifo... no one is listening?\n" );
            return FALSE;
        }
        
        if( write( fifofd, buffer, buffer_len ) == -1 )
        {
            printf( "FIFO write failed\n" );
            return FALSE;
        }
        else
        {
            close( fifofd );
            return TRUE;
        }
    }

    return FALSE;
}

int main( int argc, char* argv[] )
{
    char buffer[BUFFER_SIZE];
    int buffer_len;
    //Read stdin
    buffer_len = read(STDIN_FILENO, buffer, BUFFER_SIZE);

    //TODO: Check for fifo!
    if( check_for_fifo( buffer, buffer_len ) )
    {
        exit( 0 );
    }

    //Fork into background
    if( fork() != 0 )
        exit( 0 );

    gtk_init( &argc, &argv );

    create_query_window();

    g_thread_new( "sqlstuff", execute_input, (gpointer)buffer );

    //Create thread to listen to fifo
    mkfifo( FIFO_LOCATION, 0666 );
    g_thread_new( "fiforeader", fifo_reader, NULL );

    gtk_main();
}
