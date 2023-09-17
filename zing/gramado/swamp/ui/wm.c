/*
 * File: wm.c 
 *     The Window Manager.
 *     The window manager is embedded into the display server.
 * History:
 *     2020 - Create by Fred Nora.
 */

#include "../gwsint.h"

// #test (#bugbgu)
#define MSG_MOUSE_DOUBLECLICKED   60
#define MSG_MOUSE_DRAG            61
#define MSG_MOUSE_DROP            62

//
// Double click
//

// Delta?
static unsigned long DoubleClickSpeed=250;
unsigned long last_mousebutton_down_jiffie=0;
static int its_double_click=FALSE;


// #todo
// Maybe we need a structure for this.

// Clicked over a window.
static int grab_is_active=FALSE;
// Move the mouse without release the button.
static int is_dragging = FALSE;
static int grab_wid = -1;

// Global main structure.
// Not a pointer.
// See: window.h
struct gws_windowmanager_d  WindowManager;

// Default color scheme
struct gws_color_scheme_d* GWSCurrentColorScheme;

struct taskbar_d  TaskBar;

// -------------------------------------

// Windows - (struct)

struct gws_window_d *__root_window; 
struct gws_window_d *active_window;  // active

//
// Input events
//

// Owner
struct gws_window_d *keyboard_owner;
struct gws_window_d *mouse_owner;  // captured
// Mouse hover.
struct gws_window_d *mouse_hover;  // hover

// If the display server has a taskbar.
// maybe we don't need that.
struct gws_window_d  *taskbar_window; 
struct gws_window_d  *taskbar_startmenu_button_window; 
//char startmenu_string[32];

// ...
// z-order ?
// But we can use multiple layers.
// ex-wayland: background, bottom, top, overlay.
struct gws_window_d *first_window;
struct gws_window_d *last_window;
struct gws_window_d *top_window;     // z-order
// -------------------------------------

static const char *app1_string = "terminal.bin";
static const char *app2_string = "editor.bin";
static const char *app3_string = "browser.bin";
static const char *app4_string = "gdm.bin";
//static const char *app4_string = "cmdline.bin";

static unsigned long last_input_jiffie=0;


// Taskbar
#define TB_BUTTON_PADDING  2
// 36
//#define TB_HEIGHT  40
#define TB_HEIGHT  (24+TB_BUTTON_PADDING+TB_BUTTON_PADDING)
//#define TB_BUTTON_PADDING  4
#define TB_BUTTON_HEIGHT  (TB_HEIGHT - (TB_BUTTON_PADDING*2))
#define TB_BUTTON_WIDTH  TB_BUTTON_HEIGHT
// #define TB_BUTTONS_MAX  8


struct start_menu_d StartMenu;
struct quick_launch_d QuickLaunch;


//
// Window list.
//

// Global.
unsigned long windowList[WINDOW_COUNT_MAX];

// ---------

#define WM_DEFAULT_BACKGROUND_COLOR   COLOR_GRAY

static long old_x=0;
static long old_y=0;

//#todo
//GetWindowRect
//GetClientRect

// refresh rate of the whole screen.
static unsigned long fps=0;

// refresh rate for all dirty objects. In one round.
static unsigned long frames_count=0;
//static unsigned long frames_count_in_this_round;

static unsigned long ____old_time=0;
static unsigned long ____new_time=0;

//
// private functions: prototypes ==========================
//

static void animate_window( struct gws_window_d *window );
static void wm_tile(void);

static void on_quick_launch(int button_wid);
static void launch_app_by_id(int id);


// Process keyboard events.
static unsigned long 
wmProcessKeyboardEvent(
    int msg,
    unsigned long long1,
    unsigned long long2 );

// Process mouse events.
static void 
wmProcessMouseEvent(
    int event_type, 
    unsigned long x, 
    unsigned long y );

static void on_control_clicked_by_wid(int wid);
static void on_control_clicked(struct gws_window_d *window);

static void on_mouse_pressed(void);
static void on_mouse_released(void);
static void on_mouse_leave(struct gws_window_d *window);
static void on_mouse_hover(struct gws_window_d *window);

static void on_drop(void);

static void on_update_window(struct gws_window_d *window, int event_type);

void __probe_window_hover(unsigned long long1, unsigned long long2);

int control_action(int msg, unsigned long long1);

// Button
void __button_pressed(int wid);
void __button_released(int wid);

// Menu
void __create_start_menu(void);
void wmProcessMenuEvent(int event_number, int button_wid);

// Launch area
void __create_quick_launch_area(void);

// Key combination.
inline int is_combination(int msg_code);

static int wmProcessCombinationEvent(int msg_code);

static void wmProcessTimerEvent(unsigned long long1, unsigned long long2);

// =====================================================


void __button_pressed(int wid)
{
    if (wid < 0 || wid >= WINDOW_COUNT_MAX)
        return; 
    set_status_by_id( wid, BS_PRESSED );
    redraw_window_by_id(wid,TRUE);
}
void __button_released(int wid)
{
    if (wid < 0 || wid >= WINDOW_COUNT_MAX)
        return; 
    set_status_by_id( wid, BS_RELEASED );
    redraw_window_by_id(wid,TRUE);
}


struct gws_window_d *get_parent(struct gws_window_d *w)
{
    struct gws_window_d *p;

    if ( (void*) w == NULL )
        return NULL;
    if (w->magic != 1234){
        return NULL;
    }

    p = (struct gws_window_d *) w->parent;
    if ( (void*) p == NULL )
        return NULL;
    if (p->magic != 1234){
        return NULL;
    }

    return (struct gws_window_d *) p;
}

static void on_quick_launch(int button_wid)
{
    int ButtonWID = button_wid;
    int DoLaunch = FALSE;
    int AppID = -1;

    if (ButtonWID < 0)
        return;
    if (QuickLaunch.initialized != TRUE)
        return;
     
    if (ButtonWID == QuickLaunch.buttons[0])
    {
        DoLaunch = TRUE;
        AppID = 1;
        //launch_app_by_id(1);
        //return;
    }
    if (ButtonWID == QuickLaunch.buttons[1])
    {
        DoLaunch = TRUE;
        AppID = 2;
        //launch_app_by_id(2);
        //return;
    }
    if (ButtonWID == QuickLaunch.buttons[2])
    {
        DoLaunch = TRUE;
        AppID = 3;
        //launch_app_by_id(3);
        //return;
    }
    if (ButtonWID == QuickLaunch.buttons[3])
    {
        DoLaunch = TRUE;
        AppID = 4;
        //launch_app_by_id(4);
        //return;
    }

// Launch
    if (DoLaunch == TRUE)
        launch_app_by_id(AppID);
}

static void launch_app_by_id(int id)
{
    char name_buffer[64];

// 4 apps only
    if (id <= 0 || id > 4 )
        goto fail;

// Clear name buffer.
    memset(name_buffer,0,64-1);

    switch (id)
    {
        case 1:
            strcpy(name_buffer,app1_string);
            break;
        case 2:
            strcpy(name_buffer,app2_string);
            break;
        case 3:
            strcpy(name_buffer,app3_string);
            break;
        case 4:
            strcpy(name_buffer,app4_string);
            break;
        default:
            goto fail;
            break;
    };

// OK
    rtl_clone_and_execute(name_buffer);
    return;

fail:
    return;
}

static void wmProcessTimerEvent(unsigned long long1, unsigned long long2)
{
    struct gws_window_d *window;

    //#debug
    //printf("Tick\n");

// We need the keyboard_owner.
    window = (struct gws_window_d *) get_focus();
    if ((void*) window == NULL)
        return;
    if (window->magic != 1234)
        return;


// Print a char into the window with focus.
// It needs to be an editbox?

    // Acende
    if (window->ip_on != TRUE){
        // #todo: Create a worker.
        wm_draw_char_into_the_window(
            window, (int) '_',  COLOR_BLACK );
        wm_draw_char_into_the_window(
            window, (int) VK_BACK,  COLOR_WHITE );
        window->ip_on = TRUE;
    // Apaga
    } else if (window->ip_on == TRUE ){
        // #todo: Create a worker.
        wm_draw_char_into_the_window(
            window, (int) '_',  COLOR_WHITE );
        wm_draw_char_into_the_window(
            window, (int) VK_BACK,  COLOR_WHITE );
        window->ip_on = FALSE;
    };
}

void on_enter(void);
void on_enter(void)
{
    struct gws_window_d *window;

    // We need the keyboard_owner.
    window = (struct gws_window_d *) get_focus();
    if ((void*) window == NULL)
        return;
    if (window->magic != 1234)
        return;


// Nothing for now.
    if (window->type == WT_EDITBOX_SINGLE_LINE)
        return;


    // APAGADO: Se esta apagado, ok, apenas pinte.
    if (window->ip_on != TRUE){
        // Pinte
        //wm_draw_char_into_the_window(
            //window, (int) VK_RETURN, COLOR_BLACK );
        goto position;

    // ACESO: Se esta acesa, apague, depois pinte.
    }else if (window->ip_on == TRUE){
            
            // Apague
            // #todo: Create a worker.
            wm_draw_char_into_the_window(
                window, (int) '_',  COLOR_WHITE );
            //wm_draw_char_into_the_window(
                //window, (int) VK_RETURN,  COLOR_WHITE );
            window->ip_on = FALSE;
            goto position;
        }


position:

//--------------
// [Enter]
         
    if (window->type == WT_EDITBOX_MULTIPLE_LINES)
    {

        // bottom reached
        // Nothing for now
        if (window->ip_y >= window->height_in_chars){
            printf("y bottom\n");
            return;
        }

        window->ip_y++;
        //window->ip_x = 0;
    }
}

// #todo: explain it
static unsigned long 
wmProcessKeyboardEvent(
    int msg,
    unsigned long long1,
    unsigned long long2 )
{
    struct gws_window_d *window;
    struct gws_window_d *tmp;
    unsigned long Result=0;
    //char name_buffer[64];

// #todo
    unsigned int fg_color = 
        (unsigned int) get_color(csiSystemFontColor);
    //unsigned int bg_color = 
    //    (unsigned int) get_color(csiSystemFontColor);

    if (msg<0){
        return 0;
    }

/*
// Window with focus. 
// (keyboard_owner)
    window = (struct gws_window_d *) get_focus();
// No keyboard owner
    if ( (void*) window == NULL )
    {
        return 0;
    }
    if (window->magic != 1234)
    {
        // #bugbug
        // Invalid keyboard_owner.
        //keyboard_owner = NULL;
        return 0;
    }
*/

//================================
    if (msg == GWS_KeyDown)
    {
        // We need the keyboard_owner.
        window = (struct gws_window_d *) get_focus();
        if ((void*) window == NULL)
            return 0;
        if (window->magic != 1234)
            return 0;


        // Enter?
        if (long1 == VK_RETURN){
            on_enter();
            return 0;
        }

        // Print a char into the window with focus.
        // It needs to be an editbox?
        //#todo: on printable.
        
        // APAGADO: Se esta apagado, ok, apenas pinte.
        if (window->ip_on != TRUE){
            // Pinte
            wm_draw_char_into_the_window(
                window, (int) long1, fg_color );
        // ACESO: Se esta acesa, apague, depois pinte.
        }else{
            
            // Apague
            // #todo: Create a worker.
            wm_draw_char_into_the_window(
                window, (int) '_',  COLOR_WHITE );
            wm_draw_char_into_the_window(
                window, (int) VK_BACK,  COLOR_WHITE );
            window->ip_on = FALSE;

            // Pinte
            wm_draw_char_into_the_window(
                window, (int) long1, fg_color );        
        }
        
        // Enqueue a message into the queue that belongs
        // to the window with focus.
        // The client application is gonna get this message.
        wmPostMessage(
            (struct gws_window_d *) window,
            (int) msg,
            (unsigned long) long1,
            (unsigned long) long2);
        return 0;
    }

//================================
    if (msg == GWS_KeyUp)
    {
        // Nothing for now.
    }

//================================
    if (msg == GWS_SysKeyDown)
    {
        // Se pressionamos alguma tecla de funçao.
        // Cada tecla de funçao aciona um botao da barra de tarefas.

        // We have 4 buttons in the taskbar.
        if (long1 == VK_F1){
            __button_pressed(QuickLaunch.buttons[0]);
            return 0;
        }
        if (long1 == VK_F2){
            __button_pressed(QuickLaunch.buttons[1]);
            return 0;
        }
        if (long1 == VK_F3){
            __button_pressed(QuickLaunch.buttons[2]);
            return 0;
        }
        if (long1 == VK_F4){
            __button_pressed(QuickLaunch.buttons[3]);
            return 0;
        }

        // F9=minimize button
        // F10=maximize button
        // F11=close button
        // #todo: Explain it better.
        if (long1 == VK_F9 || 
            long1 == VK_F10 || 
            long1 == VK_F11)
        {
            control_action(msg,long1);
            return 0;
        }

        // printf("wmProcedure: [?] GWS_SysKeyDown\n");
        
        // Enfileirar a mensagem na fila de mensagens
        // da janela com foco de entrada.
        // O cliente vai querer ler isso.
        // We need the keyboard_owner.
        window = (struct gws_window_d *) get_focus();
        if ((void*) window == NULL)
            return 0;
        if (window->magic!=1234)
            return 0;
        // Post.
        wmPostMessage(
            (struct gws_window_d *) window,
            (int)msg,
            (unsigned long)long1,
            (unsigned long)long2);

        //wm_update_desktop(TRUE,TRUE); // 
        return 0;
    }

//================================
    if (msg == GWS_SysKeyUp)
    {
        // Se liberamos alguma das 4 teclas de funçao.
        // Cada tecla lança um processo filho diferente,
        // todos pre-definidos aqui.

        if (long1 == VK_F1){
            __button_released(QuickLaunch.buttons[0]);
            launch_app_by_id(1);
            //memset(name_buffer,0,64-1);
            //strcpy(name_buffer,app1_string);
            //rtl_clone_and_execute(name_buffer);
            return 0;
        }
        if (long1 == VK_F2){
            __button_released(QuickLaunch.buttons[1]);
            launch_app_by_id(2);
            //memset(name_buffer,0,64-1);
            //strcpy(name_buffer,app2_string);
            //rtl_clone_and_execute(name_buffer);
            return 0;
        }
        if (long1 == VK_F3){
            __button_released(QuickLaunch.buttons[2]);
            launch_app_by_id(3);
            //memset(name_buffer,0,64-1);
            //strcpy(name_buffer,app3_string);
            //rtl_clone_and_execute(name_buffer);
            return 0;
        }
        if (long1 == VK_F4){
            __button_released(QuickLaunch.buttons[3]);
            launch_app_by_id(4);
            //memset(name_buffer,0,64-1);
            //strcpy(name_buffer,app4_string);
            //rtl_clone_and_execute(name_buffer);
            return 0;
        }

        // F5 = Update desktop.
        // Muda o status dos botoes da quick launch area,
        // atualiza a disposiçao das janelas na tela e
        // mostra a tela.
        if (long1 == VK_F5)
        {
            set_status_by_id(QuickLaunch.buttons[0],BS_RELEASED);
            set_status_by_id(QuickLaunch.buttons[1],BS_RELEASED);
            set_status_by_id(QuickLaunch.buttons[2],BS_RELEASED);
            set_status_by_id(QuickLaunch.buttons[3],BS_RELEASED);
            WindowManager.is_fullscreen = FALSE;

            //set_input_status(FALSE);
            wm_update_desktop(TRUE,TRUE);
            //set_input_status(TRUE);
            
            return 0;
        }

        // F6 = Entra ou sai do modo fullscreen.
        if (long1 == VK_F6)
        {

            /*
            //#debug
            wm_tile();
            window_post_message_broadcast( 
                0,           // wid = Ignored
                GWS_Paint,   // msg = msg code
                0,        // long1 = 
                0 );      // long2 = 
            */

            // Enter fullscreen mode.
            if (WindowManager.is_fullscreen != TRUE)
            {
                //set_input_status(FALSE);
                wm_enter_fullscreen_mode();
                //set_input_status(TRUE);
                return 0;
            }
            // Exit fullscreen mode.
            if (WindowManager.is_fullscreen == TRUE)
            {
                //set_input_status(FALSE);
                wm_exit_fullscreen_mode(TRUE);
                //set_input_status(TRUE);
                return 0;
            }

            return 0;
        }

        // Liberamos as teclas de funçao relativas aos controles
        // de janelas.
        // #todo: Explain it better.
        if (long1 == VK_F9 || 
            long1 == VK_F10 || 
            long1 == VK_F11)
        {
            control_action(msg,long1);
            return 0;
        }
        
        // Nothing?
        
        return 0;
    }

// Not a valid msg

    return 0;
}

// Quando temos um evento de mouse,
// vamos enviar esse evento para a janela.
// Os aplicativos estao recebendo
// os eventos enviados para as janelas.
// Mas os aplicativos pegam eventos apenas da 'main window'.
static void 
wmProcessMouseEvent(
    int event_type, 
    unsigned long x, 
    unsigned long y )
{
// Process mouse events.

// Quando movemos o mouse, então atualizamos o ponteiro que indica
// a janela mouse_hover. Esse ponteiro será usado pelos eventos
// de botão de mouse.

// Window with focus.
    struct gws_window_d *w;
    unsigned long saved_x = x;
    unsigned long saved_y = y;
    unsigned long in_x=0;
    unsigned long in_y=0;
    register int Tail=0;

    if (gUseMouse != TRUE){
        return;
    }

// Error. 
// Nothing to do.
    if (event_type<0){
        return;
    }

// --------------------------------
// Move:
// Process but do not send message for now.
// #todo
// Esse eh o momento de exibirmos o cursor do mouse.
// Precisamos fazer refresh para apagar o cursor antigo
// depois pintarmos o cursor novo direto no lfb.
// Mas nao temos aqui a rotina de pintarmos direto no
// lfb.
// #todo: Trazer as rotinas de exibiçao de cursor
// para ca, depois deixar de usar
// as rotinas de pintura de cursor que estao no kernel.
    if (event_type == GWS_MouseMove)
    {
        // If we already clicked the window
        // and now we're moving it.
        // So, now we're dragging it.
        
        // O botao esta pressionado.
        if (grab_is_active == TRUE){
            is_dragging = TRUE;
        // O botao nao esta pressionado.
        } else if (grab_is_active != TRUE){
            is_dragging = FALSE;
        };
        //?
        set_refresh_pointer_status(TRUE);
        // Update the global mouse position.
        // The compositor is doing its job,
        // painting the pointer in the right position.
        // Lets update the position. See: comp.c
        comp_set_mouse_position(saved_x,saved_y);
        // Check the window we are inside of 
        // and update the mouse_hover pointer.
        __probe_window_hover(saved_x,saved_y);

        return;
    }

// --------------------------------
// Pressed:
// Process but do not send message for now.
    if (event_type == GWS_MousePressed){
        on_mouse_pressed();
        return;
    }

// --------------------------------
// Released:
// Process and send message.
    if (event_type == GWS_MouseReleased){
        on_mouse_released();
        return;
    }

    if (event_type == MSG_MOUSE_DOUBLECLICKED)
    {
        printf("MSG_MOUSE_DOUBLECLICKED: #todo\n");
    }

    // ...

// Not valid event type
not_valid:
    return;
}


static void on_mouse_pressed(void)
{
// #todo
// When the mouse was pressed over en editbox window.
// So, set the focus?

    int ButtonID = -1;

// Validating the window mouse_over.
    if ( (void*) mouse_hover == NULL ){
        return;
    }
    if (mouse_hover->magic != 1234){
        return;
    }

    //if (mouse_hover == __root_window)
        //return;

    ButtonID = (int) mouse_hover->id;

// Now we have a new mouse_owner.
// #todo
// Maybe we can send a message to this window,
// and the client can make all the changes it wants.
    mouse_owner = mouse_hover;
    //mouse_owner->mouse_x = 0;
    //mouse_owner->mouse_y = 0;

// -------------------------
// #test
// Regular button and quick launch button.
// Not a control, not the start menu, not the menuitem.
    if (mouse_hover->type == WT_BUTTON)
    {
        if ( mouse_hover->isControl != TRUE &&
             mouse_hover->id != StartMenu.wid &&
             mouse_hover->isMenuItem != TRUE )
        {
            __button_pressed(ButtonID);
            return;
        }
    }

// -------------------------
//#test
// Start menu button.
    if (mouse_hover->id == StartMenu.wid)
    {
        __button_pressed(ButtonID);
        return;
    }

//
// Title bar
//

    struct gws_window_d *p;

    // When we pressed the titlebar,
    // actually we're grabbing the parent.
    if (mouse_hover->isTitleBar == TRUE)
    {
        grab_wid = -1; // No grabbed window
        grab_is_active = FALSE;  // Not yet
        // We're not dragging yet.
        // Just clicked. (Not moving yet).
        is_dragging = FALSE;

        //#wrong
        //grab_wid = (int) mouse_hover->id;
        
        // parent
        p = (struct gws_window_d *) mouse_hover->parent;
        if ( (void*) p != NULL )
        {
            // Valid parent. 
            if (p->magic == 1234)
            {
                // Binbaquera!
                // It needs to be an overlapped window
                // Grab it!
                if (p->type == WT_OVERLAPPED)
                {
                    grab_wid = (int) p->id;
                    grab_is_active = TRUE;            
                }
            }
        }

        return;
    }

//
// Controls - (Title bar buttons).
//

// ===================================
// >> Minimize control
// Redraw the button
    if (mouse_hover->isMinimizeControl == TRUE)
    {
        if (mouse_hover->type == WT_BUTTON)
        {
            __button_pressed(ButtonID);
            return;
        }
    }
// ===================================
// >> Maximize control
// Redraw the button
    if (mouse_hover->isMaximizeControl == TRUE)
    {
        if (mouse_hover->type == WT_BUTTON)
        {
            __button_pressed(ButtonID);
            return;
        }
    }
// ===================================
// >> Close control
// Redraw the button
    if (mouse_hover->isCloseControl == TRUE)
    {
        if (mouse_hover->type == WT_BUTTON)
        {
            __button_pressed(ButtonID);
            return;
        }
    }

//
// Menu item
// 

// ===================================
// >> menuitem
// Lidando com menuitens
// Se clicamos em um menu item.
// Redraw the button
    if (mouse_hover->isMenuItem == TRUE)
    {
        if (mouse_hover->type == WT_BUTTON)
        {
            __button_pressed(ButtonID);
            return;
        }
    }
}

// When clicked or 'pressed' via keyboard.
static void on_control_clicked_by_wid(int wid)
{
    struct gws_window_d *window;

    if (wid<0)
        return;
    window = (struct gws_window_d *) get_window_from_wid(wid);
    if ((void*) window == NULL)
        return;
    if (window->magic != 1234){
        return;
    }

    on_control_clicked(window);
}

// Let's check what control button was clicked.
// The control is a window that belongs to the titlebar
// of an overlapped window.
// When clicked or 'pressed' via keyboard.
static void on_control_clicked(struct gws_window_d *window)
{
// Called when a control button was release.
// (Clicked by the mouse or some other input event)
// + The button belongs to a title bar.
// + The titlebar belongs to an overlapped.
// #tests:
// + Post message to close the overlapped
//   if the button is a close control.

    struct gws_window_d *w1;
    struct gws_window_d *w2;

// ------------------------------
// + Post message to close the overlapped
//   if the button is a close control.

    if ((void*) window == NULL)
        return;
    if (window->magic != 1234)
        return;

// It was not clicked.
// When the mouse is the only valid input device.
    //if (window != mouse_hover)
        //return;

// ------------
// Minimize control
    if (window->isMinimizeControl == TRUE)
    {
        // The parent of the controle is the titlebar.
        w1 = (void*) window->parent;
        if ((void*) w1 != NULL)
        {
            if (w1->magic == 1234)
            {
                // The parent of the titlebar is the main window.
                w2 = (void*) w1->parent;
                if ((void*) w2 != NULL)
                {
                    if (w2->magic == 1234)
                    {
                        // Check if it is an overlapped window and 
                        // Post a message to the main window
                        // of the client app.
                        if (w2->type == WT_OVERLAPPED)
                        {
                            printf("Minimize\n");
                            //window_post_message ( 
                            //    w2->id,
                            //    GWS_Minimize
                            //    0,
                            //    0 );
                            return;
                        }
                    }
                }
            }
        }
    }

// ------------
// Maximize control
    if (window->isMaximizeControl == TRUE)
    {
        // The parent of the controle is the titlebar.
        w1 = (void*) window->parent;
        if ((void*) w1 != NULL)
        {
            if (w1->magic == 1234)
            {
                // The parent of the titlebar is the main window.
                w2 = (void*) w1->parent;
                if ((void*) w2 != NULL)
                {
                    if (w2->magic == 1234)
                    {
                        // Check if it is an overlapped window and 
                        // Post a message to the main window
                        // of the client app.
                        if (w2->type == WT_OVERLAPPED)
                        {
                            printf("Maximize\n");
                            //window_post_message ( 
                            //    w2->id,
                            //    GWS_Maximize,
                            //    0,
                            //    0 );
                            return;
                        }
                    }
                }
            }
        }
    }

// ------------
// Close control
// A close control was cliked.
    if (window->isCloseControl == TRUE)
    {
        // The parent of the controle is the titlebar.
        w1 = (void*) window->parent;
        if ((void*) w1 != NULL)
        {
            if (w1->magic == 1234)
            {
                // The parent of the titlebar is the main window.
                w2 = (void*) w1->parent;
                if ((void*) w2 != NULL)
                {
                    if (w2->magic == 1234)
                    {
                        // Check if it is an overlapped window and 
                        // Post a message to the main window
                        // of the client app.
                        if (w2->type == WT_OVERLAPPED)
                        {
                            printf("Close wid={%d}\n",w2->id);
                            window_post_message ( 
                                w2->id,
                                GWS_Close,
                                0,
                                0 );
                            return;
                        }
                    }
                }
            }
        }
    }
}

static void on_mouse_released(void)
{
    // Get these from event.
    unsigned long saved_x=0;
    unsigned long saved_y=0;
    // Relative to the mouse_hover.
    unsigned long in_x=0;
    unsigned long in_y=0;

    int ButtonWID = -1;

    struct gws_window_d *p;
    struct gws_window_d *tmp;
    struct gws_window_d *old_focus;

    unsigned long x=0;
    unsigned long y=0;

    unsigned long x_diff=0;
    unsigned long y_diff=0;


    // #hackhack
    int event_type = GWS_MouseReleased;

    // button number
    //if(long1==1){ yellow_status("R1"); }
    //if(long1==2){ yellow_status("R2"); }
    //if(long1==3){ yellow_status("R3"); }

// Post it to the client. (app).
// When the mouse is on any position in the screen.
// #bugbug
// Essa rotina envia a mensagem apenas se o mouse
// estiver dentro da janela com foco de entrada.

/*
    wmProcessMouseEvent( 
        GWS_MouseReleased,              // event type
        comp_get_mouse_x_position(),    // current cursor x
        comp_get_mouse_y_position() );  // current cursor y
*/

// When the mouse is hover a tb button.

    // safety first.
    // mouse_hover validation

    if ((void*) mouse_hover == NULL)
        return;
    if (mouse_hover->magic != 1234)
        return;


// If the mouse is hover a button.
    ButtonWID = (int) mouse_hover->id;
// If the mouse is hover an editbox.
    saved_x = comp_get_mouse_x_position();
    saved_y = comp_get_mouse_y_position();

// -------------------
// Se clicamos em uma janela editbox.

    static int InsideStatus=FALSE;

// Check if we are inside the mouse hover.
    if ( saved_x >= mouse_hover->absolute_x &&
         saved_x <= mouse_hover->absolute_right &&
         saved_y >= mouse_hover->absolute_y &&
         saved_y <= mouse_hover->absolute_bottom )
    {
        // #debug
        // printf("Inside mouse hover window :)\n");
    
        InsideStatus = TRUE;

        // Values inside the window.
        in_x = (unsigned long) (saved_x - mouse_hover->absolute_x);
        in_y = (unsigned long) (saved_y - mouse_hover->absolute_y);

        // Change the input pointer
        // inside the window editbox.
        if ( mouse_hover->type == WT_EDITBOX || 
             mouse_hover->type == WT_EDITBOX_MULTIPLE_LINES)
        {
            // Se o evento for released,
            // então mudamos o input pointer.
            if (event_type == GWS_MouseReleased)
            {
                // Set the new input pointer for this window.
                if (in_x>0){
                    mouse_hover->ip_x = (unsigned long) (in_x/8);
                }
                if (in_y>0){
                    mouse_hover->ip_y = (unsigned long) (in_y/8);
                }
 
                // Set the new keyboard owner. (focus)
                if (mouse_hover != keyboard_owner){
                    set_focus(mouse_hover);
                }

                // Set the new mouse owner.
                if (mouse_hover != mouse_owner){
                    mouse_owner = mouse_hover;
                }
            }
            
            mouse_hover->single_event.has_event = FALSE;            

            // Post message to the target window.
            // #remember:
            // The app get events only for the main window.
            // This way the app can close the main window.
            // #todo: 
            // So, we need to post a message to the main window,
            // telling that that message affects the client window.
        
            // #bugbug
            // Na verdade o app so le a main window.
            window_post_message( mouse_hover->id, event_type, in_x, in_y );
            //------------------
            // done for editbox. return
            return;
        }
        //fail
        mouse_hover->single_event.has_event = FALSE;
    }

    // #debug
    // printf("Outside mouse hover window\n");

// -------------------------
// Regular button or quick launch button.
// Not a control, not the start menu, not the menuitem.
    if (mouse_hover->type == WT_BUTTON)
    {
        if ( mouse_hover->isControl != TRUE &&
             mouse_hover->id != StartMenu.wid &&
             mouse_hover->isMenuItem != TRUE )
        {
            __button_released(ButtonWID);
            
            // Is a quick launch button?
            // Se for um dos quatro botoes da quick launch.
            if ( ButtonWID == QuickLaunch.buttons[0] || 
                 ButtonWID == QuickLaunch.buttons[1] || 
                 ButtonWID == QuickLaunch.buttons[2] || 
                 ButtonWID == QuickLaunch.buttons[3] )
            {
                on_quick_launch(ButtonWID);
            }
            return;
        }
    }

//------------------------------------------------------
//
// Start menu button.
//

// -------------------
// #test
// Start menu button was released.
    if (ButtonWID == StartMenu.wid){
        wmProcessMenuEvent(MENU_EVENT_RELEASED,StartMenu.wid);
        return;
    }

//
// Grabbing a window.
//

// + We already pressed a window.
// + We already moved the mouse. (Drag is active).
// Now it's time to drop it. (Releasing the button).
// and setting the grab as not active.

// Drop event
// Release when dragging.
    if ( grab_is_active == TRUE &&  
         is_dragging == TRUE &&       
         grab_wid > 0 )
    {
        on_drop();
        return;
    }

    //if(long1==1){ yellow_status("R1"); }
    //if(long1==2){ yellow_status("R2"); wm_update_desktop(TRUE,TRUE); return 0; }
    //if(long1==1){ 
        //yellow_status("R1"); 
        //create_main_menu(8,8);
        //return; 
    //}
    //if(long1==3){ yellow_status("R3"); return 0; }
    //if(long1==2){ create_main_menu(mousex,mousey); return 0; }
    //if(long1==2){ create_main_menu(mousex,mousey); return 0; }

//
// Titlebar
//

// ===================================
// Title bar
// Release the titlebar.
    if (mouse_hover->isTitleBar == TRUE)
    {
        // Get parent.
        p = (struct gws_window_d *) mouse_hover->parent;
        if ((void*) p != NULL)
        {
            if (p->magic == 1234)
            {
                // Set as last window and update desktop.
                if (p->type == WT_OVERLAPPED)
                {
                    // Get old active, deactivate and redraw the old.
                    tmp = (struct gws_window_d *) get_active_window();
                    unset_active_window();
                    redraw_window(tmp,TRUE);
                    on_update_window(tmp,GWS_Paint);
                    
                    // Set new active and redraw.
                    set_active_window(p);
                    //set_focus(p);
                    redraw_window(p,TRUE);
                    on_update_window(p,GWS_Paint);  // to wwf
                    //on_update_window(p,GWS_Paint);

                    // ?
                    //old_focus = (void*) get_focus(); 
                    //if ((void*) old_focus != NULL )
                    //set_active_window(tmp);
                    //set_focus(tmp);
                    //redraw_window(tmp,TRUE);

                    // Set the last window and update the desktop.
                    //wm_update_desktop3(p);
                    
                    return;
                }
            }
        }
    }


//
// Controls - (Titlebar buttons).
//

// ===================================
// >> Minimize control
// Redraw the button
    if (mouse_hover->isMinimizeControl == TRUE)
    {
        if (mouse_hover->type == WT_BUTTON)
        {
            __button_released(ButtonWID);
            //printf("Release on min control\n");
            on_control_clicked(mouse_hover);
            return;
        }
    }
// ===================================
// >> Maximize control
// Redraw the button
    if (mouse_hover->isMaximizeControl == TRUE)
    {
        if (mouse_hover->type == WT_BUTTON)
        {
            __button_released(ButtonWID);
            //printf("Release on max control\n");
            on_control_clicked(mouse_hover);
            return;
        }
    }
// ===================================
// >> Close control
// Redraw the button
    if (mouse_hover->isCloseControl == TRUE)
    {
        if (mouse_hover->type == WT_BUTTON)
        {
            __button_released(ButtonWID);
            //printf("Release on close control\n");
            // #test
            // On control clicked
            // close control: post close message.
            on_control_clicked(mouse_hover);
            return;
        }
    }


//
// Menu itens
//

// ===================================
// >> Menuitens
// Lidando com menuitens
// Se clicamos em um menu item.
// Redraw the button
    unsigned long selected_item=0;
    if (mouse_hover->isMenuItem == TRUE)
    {
        if (mouse_hover->type == WT_BUTTON)
        {
            __button_released(ButtonWID);
            selected_item = (unsigned long) (ButtonWID & 0xFFFF);
            menuProcedure(
                NULL,
                (int) 1,
                (unsigned long) selected_item,
                (unsigned long) BS_RELEASED );

            return;
        }
    }
}

// Post a message into the window with focus message queue.
// #todo: We need a worker for posting messages into the queues.
// Do we already have one?
static void on_update_window(struct gws_window_d *window, int event_type)
{
// Post a message to the window with focus.

// Window with focus.
    struct gws_window_d *w;
    register int Tail=0;

// Error. Nothing to do.
    if (event_type<0){
        return;
    }

    //w = (struct gws_window_d *) get_focus();
    w = (struct gws_window_d *) window;
    if ( (void*) w==NULL ){ return; }
    if (w->magic != 1234) { return; }

// Update window structure.
// #?? We are using a single event.
// But maybe we can use a queue, just like the 
// messages in the thread structure.

    // No more single events
    //w->single_event.wid   = w->id;
    //w->single_event.msg   = event_type;
    //w->single_event.long1 = 0;
    //w->single_event.long2 = 0;
    //w->single_event.has_event = TRUE;
    //w->single_event.has_event = FALSE;

/*
// ---------------
// Post message
    Tail = (int) w->ev_tail;
    w->ev_wid[Tail]   = (unsigned long) (w->id & 0xFFFFFFFF);
    w->ev_msg[Tail]   = (unsigned long) (event_type & 0xFFFFFFFF);
    w->ev_long1[Tail] = (unsigned long) 0; 
    w->ev_long2[Tail] = (unsigned long) 0;
    w->ev_tail++;
    if (w->ev_tail >= 32){
        w->ev_tail=0;
    }
// ---------------
 */

// Post message
    window_post_message( w->id, event_type, 0, 0 );
}


// Um evento afeta os controles de janela.
// Vamos pressionar ou liberar um dos botoes de controle
// que estao na barra de titulos.
// IN: event, key
int control_action(int msg, unsigned long long1)
{
// #todo
// Explain the parameters.
// It affects the active window.

    struct gws_window_d *aw;
    struct gws_window_d *w;
    int minimize_wid =-1;
    int maximize_wid =-1;
    int close_wid=-1;

    if (msg<0){
        goto fail;
    }

//
// Get the active window.
//

// #bugbug
// Maybe it is not working when we
// are trying to close a lot of windows with 
// 5 windows ot more.

    aw = (void*) get_active_window();
    if ((void*) aw == NULL)
    {
        //#debug
        //printf("control_action: No aw\n");
        //exit(0);
        goto fail;
    }
    if (aw->magic != 1234)
    {
        goto fail;
    }
    // Overlapped window?
    // #todo: A janela ativa pode ser de mais de um tipo.
    //if (aw->type != WT_OVERLAPPED)
        //goto fail;

// -----------------------
// titlebar
// #bugbug: 
// Nem todos tipos de janela possuem uma titlebar.
// Precisa ser overlapped?

    w = (void*) aw->titlebar;
    if ((void*) w == NULL){
        goto fail;
    }
    if (w->magic != 1234){
        goto fail;
    }

// Is the control support already initialized for this window?
    if (w->Controls.initialized != TRUE){
        goto fail;
    }

// Get WIDs for the controls.
    minimize_wid = (int) w->Controls.minimize_wid;
    maximize_wid = (int) w->Controls.maximize_wid;
    close_wid    = (int) w->Controls.close_wid;

//
// The message.
//

    switch (msg){

    // Quando a tecla foi pressionada,
    // mudaremos o status, repintamos e mostramos o botao.
    case GWS_SysKeyDown:
        if (long1==VK_F9){
            __button_pressed(minimize_wid);
            return 0;
        }
        if (long1==VK_F10){
            __button_pressed(maximize_wid);
            return 0;
        }
        if (long1==VK_F11){
            __button_pressed(close_wid);
            return 0;
        }
        return 0;
        break;

    // Quando a tecla for liberada,
    // mudaremos o status, repintamos e mostramos o botao.
    // ?
    case GWS_SysKeyUp:
        if (long1 == VK_F9){
            __button_released(minimize_wid);
            on_control_clicked_by_wid(minimize_wid);
            return 0;
        }
        if (long1 == VK_F10){
            __button_released(maximize_wid);
            on_control_clicked_by_wid(maximize_wid);
            return 0;
        }
        // The close control was released.
        if (long1 == VK_F11){
            __button_released(close_wid);
            on_control_clicked_by_wid(close_wid);
            return 0;
        }
        break;

    default:
        break;
    };

fail:
    return (int) -1;
}

void show_client( struct gws_client_d *c, int tag )
{
    if ( (void*) c == NULL ){
        return;
    }
    if (c->magic!=1234){
        return;
    }

    if (tag<0){
        return;
    }
    if (tag >= 4){
        return;
    }

// This client doesn't belong to this tag.
    if ( c->tags[tag] != TRUE ){
        return;
    }

    if (c->window < 0){
        return;
    }
    if (c->window >= WINDOW_COUNT_MAX){
        return;
    }

// Show
    if (c->window == WT_OVERLAPPED){
        redraw_window_by_id(c->window,TRUE);
    }

// Show next.
    //show_client(c->next,tag);
}

//#todo: networking
void show_client_list(int tag)
{
    struct gws_client_d *c;

    c = (struct gws_client_d *) first_client;
    while (1){
        if ( (void*) c == NULL ){
            break;
        }
        if ( (void*) c != NULL ){
            show_client(c,tag);
        }
        c = (struct gws_client_d *) c->next;
    };
}

//#todo: Not teste yet.
struct gws_client_d *wintoclient(int window)
{
    struct gws_client_d *c;

    if (window<0){
        return NULL;
    }
    if (window>=WINDOW_COUNT_MAX){
        return NULL;
    }

    c = (struct gws_client_d *) first_client;
    while ( (void*) c != NULL )
    {
        if (c->magic == 1234)
        {
            if (c->window == window){
                return (struct gws_client_d *) c;
            }
        }
        c = (struct gws_client_d *) c->next;
    };
    return NULL;
}

void __set_default_background_color(unsigned int color)
{
    WindowManager.default_background_color = (unsigned int) color;
}

unsigned int __get_default_background_color(void)
{
    return (unsigned int) WindowManager.default_background_color;
}

void __set_custom_background_color(unsigned int color)
{
    WindowManager.custom_background_color = (unsigned int) color;
    WindowManager.has_custom_background_color = TRUE;
}

unsigned int __get_custom_background_color(void)
{
    return (unsigned int) WindowManager.custom_background_color;
}

int __has_custom_background_color(void)
{
    if (WindowManager.has_custom_background_color == TRUE){
        return TRUE;
    }
    return FALSE;
}

int __has_wallpaper(void)
{
    if (WindowManager.has_wallpaper == TRUE){
        return TRUE;
    }
    return FALSE;
}

// Called by main.c
void wmInitializeStructure(void)
{
    unsigned int bg_color = 
        (unsigned int) get_color(csiDesktop);

// Clear the structure.
    WindowManager.mode = WM_MODE_TILED;  //tiling
// Orientation
    WindowManager.vertical = FALSE;   // horizontal. default
    //WindowManager.vertical = TRUE;
// How many frames until now.
    WindowManager.frame_counter = 0;
    WindowManager.fps = 0;

// At this moment we don't have a root window.
    WindowManager.root = NULL;
// At this moment we don't have a taskbar window.
    WindowManager.taskbar = NULL;

// #todo
// Desktop composition.
// #todo
// Create methods so enable and disable this feature.
    WindowManager.comp.use_transparence = FALSE;
    WindowManager.comp.is_enabled = FALSE;
    WindowManager.comp.use_visual_effects = FALSE;
    WindowManager.comp.initialized = TRUE;

// fullscreen support.
    WindowManager.is_fullscreen = FALSE;
    //WindowManager.is_fullscreen = TRUE;
    WindowManager.fullscreen_window = NULL;

    //WindowManager.box1 = NULL;
    //WindowManager.box2 = NULL;
    //WindowManager.tray1 = NULL;
    //WindowManager.tray2 = NULL;

// #todo
// Desktop rectangles


// Working area.
// Área de trabalho.
// Container, not a window.
    WindowManager.wa.left = 0;
    WindowManager.wa.top = 0;
    WindowManager.wa.width = 0;
    WindowManager.wa.height = 0;

//
// Background color
//

// Default background color.
    __set_default_background_color(bg_color);
// Default background color.
    __set_custom_background_color(bg_color);

    WindowManager.has_custom_background_color = FALSE;
// Wallpaper
    WindowManager.has_wallpaper = FALSE;
// Has loadable theme.
    WindowManager.has_theme = FALSE;
// Not initialized yet.
// We need to setup the windows elements.
    WindowManager.initialized = FALSE;

    Initialization.wm_struct_checkpoint = TRUE;
}


// Internal
// Called by wm_process_windows().

void __update_fps(void)
{
    unsigned long dt=0;
    char rate_string[32];

    //debug_print ("__update_fps:\n");

// counter
    frames_count++;

//
// == time =========================================
//

// #bugbug
// We have a HUGE problem here.
// We can't properly get the data inside the structures. 
// The value is not the same when we enter inside the kernel via
// keyboard interrupt or via system interrupt.

// get current time.

// #bugbug
// A variável do indice 120 não esta sendo usada.
// Vamos tentar a variável do indice 118, que é a jiffies.

    //____new_time = rtl_get_progress_time();
    //____new_time = (unsigned long) rtl_get_system_metrics (120);
    ____new_time = (unsigned long) rtl_get_system_metrics (118);

// delta
    dt = (unsigned long) (____new_time - ____old_time);

    ____old_time = ____new_time;

    fps = (1000/dt);

// mostra 
    //if ( show_fps_window == TRUE )
    //{
        //itoa(____new_time,rate_string);
        //itoa(dt,rate_string);
        itoa(fps,rate_string);
        yellow_status(rate_string);
    //}

    return;

    //if(dt<8)
        //return;

//=============================================================
// ++  End

    //t_end = rtl_get_progress_time();
    //__refresh_rate =  t_end - t_start;
    //__refresh_rate = __refresh_rate/1000;
    //printf ("@ %d %d %d \n",__refresh_rate, t_now, t_old);

//====================================
// fps++
// conta quantos frames. 

    // se passou um segundo.
    //if ( dt > 1000 )
    if ( dt > 8 )
    {
        // Save old time.
        ____old_time = ____new_time;
        
        //fps = frames_count; // quantos frames em 1000 ms aproximadamente?
        //itoa(fps,rate_string); 

        itoa(dt,rate_string); // mostra o delta.

        //if ( show_fps_window == TRUE ){
            yellow_status(rate_string);
        //}

        // Clean for next round.
        frames_count=0;
        fps=0;
        dt=0;
    }
    //fps--
    //=======================

    //debug_print ("__update_fps: done\n");
}


// Criando controles para uma tilebar ou 
// outro tipo de janela talvez.
// The title bar has controls.
//tbWindow->Controls.minimize = NULL;
//tbWindow->Controls.maximize = NULL;
//tbWindow->Controls.close = NULL;
// IN:
// Titlebar window pointer.

void do_create_controls(struct gws_window_d *w_titlebar)
{

// Windows
    struct gws_window_d *w_minimize;
    struct gws_window_d *w_maximize;
    struct gws_window_d *w_close;

    int id=-1;

// Colors for the button
    unsigned int bg_color =
        (unsigned int) get_color(csiButton);
    //unsigned int bg_color =
    //    (unsigned int) get_color(csiButton);


    if ((void*)w_titlebar == NULL){
        return;
    }
    if (w_titlebar->magic != 1234){
        return;
    }
    //if(window->isTitleBar!=TRUE)
    //    return;

    w_titlebar->Controls.minimize_wid = -1;
    w_titlebar->Controls.maximize_wid = -1;
    w_titlebar->Controls.close_wid    = -1;
    w_titlebar->Controls.initialized = FALSE;

// Buttons
    unsigned long ButtonWidth = 
        METRICS_TITLEBAR_CONTROLS_DEFAULT_WIDTH;
    unsigned long ButtonHeight = 
        METRICS_TITLEBAR_CONTROLS_DEFAULT_HEIGHT;

    unsigned long LastLeft = 0;

    unsigned long TopPadding=1; //2;  // Top margin
    unsigned long RightPadding=2;  // Right margin
    
    unsigned long SeparatorWidth=1;

// #test
// #bugbug
    //Top=1;
    //ButtonWidth  = (unsigned long) (w_titlebar->width -4);
    //ButtonHeight = (unsigned long) (w_titlebar->height -4);

// ================================================
// minimize
    LastLeft = 
        (unsigned long)( 
            w_titlebar->width - 
            (3*ButtonWidth) - 
            (2*SeparatorWidth) - 
            RightPadding );

    w_minimize = 
        (struct gws_window_d *) CreateWindow ( 
            WT_BUTTON, 0, 1, 1, 
            "_",  //string  
            LastLeft,  //l 
            TopPadding, //t 
            ButtonWidth, 
            ButtonHeight,   
            w_titlebar, 0, bg_color, bg_color );

    if ( (void *) w_minimize == NULL ){
        //gwssrv_debug_print ("xx: minimize fail \n");
        return;
    }
    if (w_minimize->magic!=1234){
        return;
    }
    w_minimize->isControl = TRUE;

    w_minimize->left_offset = 
        (unsigned long) (w_titlebar->width - LastLeft);

    w_minimize->type = WT_BUTTON;
    w_minimize->isMinimizeControl = TRUE;
    w_minimize->bg_color_when_mousehover = 
        (unsigned int) get_color(csiWhenMouseHoverMinimizeControl);

    id = RegisterWindow(w_minimize);
    if (id<0){
        gwssrv_debug_print("xxx: Couldn't register w_minimize\n");
        return;
    }
    w_titlebar->Controls.minimize_wid = (int) id;

// ================================================
// maximize
    LastLeft = 
        (unsigned long)(
        w_titlebar->width - 
        (2*ButtonWidth) - 
        (1*SeparatorWidth) - 
        RightPadding );

    w_maximize = 
        (struct gws_window_d *) CreateWindow ( 
            WT_BUTTON, 0, 1, 1, 
            "M",  //string  
            LastLeft,  //l 
            TopPadding, //t 
            ButtonWidth, 
            ButtonHeight,   
            w_titlebar, 0, bg_color, bg_color );

    if ( (void *) w_maximize == NULL ){
        //gwssrv_debug_print ("xx: w_maximize fail \n");
        return;
    }
    if (w_maximize->magic!=1234){
        return;
    }
    w_maximize->isControl = TRUE;
    
    w_maximize->left_offset = 
        (unsigned long) (w_titlebar->width - LastLeft);

    w_maximize->type = WT_BUTTON;
    w_maximize->isMaximizeControl = TRUE;
    w_maximize->bg_color_when_mousehover = 
        (unsigned int) get_color(csiWhenMouseHoverMaximizeControl);

    id = RegisterWindow(w_maximize);
    if (id<0){
        gwssrv_debug_print ("xxx: Couldn't register w_maximize\n");
        return;
    }
    w_titlebar->Controls.maximize_wid = (int) id;

// ================================================
// close
    LastLeft = 
        (unsigned long)(
        w_titlebar->width - 
        (1*ButtonWidth)  - 
         RightPadding );

    w_close = 
        (struct gws_window_d *) CreateWindow ( 
            WT_BUTTON, 0, 1, 1, 
            "X",       //string  
            LastLeft,  //l 
            TopPadding, //t 
            ButtonWidth, 
            ButtonHeight,   
            w_titlebar, 0, bg_color, bg_color );

    if ( (void *) w_close == NULL ){
        //gwssrv_debug_print ("xx: w_close fail \n");
        return;
    }
    if (w_close->magic!=1234){
        return;
    }
    w_close->isControl = TRUE;
    
    w_close->left_offset = 
        (unsigned long) (w_titlebar->width - LastLeft);

    w_close->type = WT_BUTTON;
    w_close->isCloseControl = TRUE;
    w_close->bg_color_when_mousehover = 
        (unsigned int) get_color(csiWhenMouseHoverCloseControl);

    id = RegisterWindow(w_close);
    if (id<0){
        gwssrv_debug_print ("xxx: Couldn't register w_close\n");
        return;
    }
    w_titlebar->Controls.close_wid = (int) id;

    w_titlebar->Controls.initialized = TRUE;
}

// Create titlebar and controls.
struct gws_window_d *do_create_titlebar(
    struct gws_window_d *parent,
    unsigned long tb_height,
    unsigned int color,
    unsigned int ornament_color,
    int has_icon,
    int icon_id,
    int has_string )
{
// Respect the border size
// of the parent.

    struct gws_window_d *tbWindow;
    // The position and the dimensions depends on the
    // border size.
    unsigned long TitleBarLeft=0;
    unsigned long TitleBarTop=0;
    unsigned long TitleBarWidth=0;
    unsigned long TitleBarHeight = tb_height;  //#todo metrics
    // Color and rop.
    unsigned int TitleBarColor = color;
    unsigned long rop=0;

    if ( (void*) parent == NULL )
        return NULL;
    if (parent->magic!=1234)
        return NULL;

// Se a parent é uma overlapped  e esta maximizada
// então faremos uma title bar diferente,
// que preencha todo o topo da tela.
// Nesse caso a parent não deve ter borda.
    int IsMaximized=FALSE;
    if (parent->type == WT_OVERLAPPED &&
        parent->style == WS_MAXIMIZED )
    {
        IsMaximized=TRUE;
    }

// border size.
// Respect the border size
// of the parent.
    //unsigned long BorderSize = parent->border_size;

// #todo: 
// Different position, depending on the style
// if the parent is maximized or not.
// Without border, everything changes.
    if (IsMaximized==TRUE)
    {
        TitleBarLeft  = 0;
        TitleBarTop   = 0;
        TitleBarWidth = parent->width;
    }
// A parent não está maximizada,
// então considere diminuir a largura, 
// para incluir as bordas.
    if (IsMaximized != TRUE)
    {
        TitleBarLeft = parent->border_size;
        TitleBarTop  = parent->border_size;
        // border size can't be '0'.
        //if (parent->border_size==0){
        //    printf ("bsize%d\n",parent->border_size);
        //    while(1){}
        //}
        TitleBarWidth = (parent->width - parent->border_size - parent->border_size);
    }

// Save
    parent->titlebar_height = TitleBarHeight;
    parent->titlebar_width = TitleBarWidth;
    parent->titlebar_color = (unsigned int) TitleBarColor;
    parent->titlebar_text_color = 
        (unsigned int) get_color(csiSystemFontColor);

// Herda o rop.
    rop = parent->rop;

//-----------

// #important: 
// WT_SIMPLE with text.
// lembre-se estamos relativos à area de cliente
// da janela mão, seja ela de qual tipo for.
    tbWindow = 
       (void *) doCreateWindow ( 
                    WT_TITLEBAR, 0, 1, 1, "TitleBar", 
                    TitleBarLeft, 
                    TitleBarTop, 
                    TitleBarWidth, 
                    TitleBarHeight, 
                    (struct gws_window_d *) parent, 
                    0, 
                    TitleBarColor,  //frame 
                    TitleBarColor,  //client
                    (unsigned long) rop );   // rop_flags from the parent 

    if ((void *) tbWindow == NULL){
        gwssrv_debug_print ("do_create_titlebar: tbWindow\n");
        return NULL;
    }
    tbWindow->type = WT_SIMPLE;
    tbWindow->isTitleBar = TRUE;

// No room drawing more stuff inside the tb window.
    if (tbWindow->width == 0)
        return NULL;

// --------------------------------
// Icon

    int useIcon = has_icon;  //#HACK
    parent->titlebarHasIcon = FALSE;

// O posicionamento em relação
// à janela é consistente por questão de estilo.
// See: bmp.c
// IN: index, left, top.
// Icon ID:
// Devemos receber um argumento do aplicativo,
// na hora da criação da janela.

    if (icon_id < 1 || icon_id > 5)
    {
        //icon_id = 1;
        printf("do_create_titlebar: Invalid icon id\n");
        return NULL;
    }

    parent->frame.titlebar_icon_id = icon_id;

// Decode the bmp that is in a buffer
// and display it directly into the framebuffer. 
// IN: index, left, top
// see: bmp.c
    unsigned long iL=0;
    unsigned long iT=0;
    unsigned long iWidth = 16;

    if (useIcon == TRUE)
    {
        iL = (unsigned long) (tbWindow->absolute_x + METRICS_ICON_LEFTPAD);
        iT = (unsigned long) (tbWindow->absolute_y + METRICS_ICON_TOPPAD);
        bmp_decode_system_icon( 
            (int) icon_id, 
            (unsigned long) iL, 
            (unsigned long) iT,
            FALSE );
        parent->titlebarHasIcon = TRUE;
    }

// ---------------------------
// Ornament
    // int useOrnament = TRUE;

// Ornamento:
// Ornamento na parte de baixo da title bar.
// #important:
// O ornamento é pintado dentro da barra, então isso
// não afetará o positionamento da área de cliente.
// border on bottom.
// Usado para explicitar se a janela é ativa ou não
// e como separador entre a barra de títulos e a segunda
// área da janela de aplicativo.
// Usado somente por overlapped window.

    unsigned int OrnamentColor1 = ornament_color;
    unsigned long OrnamentHeight = METRICS_TITLEBAR_ORNAMENT_SIZE;
    if (IsMaximized == TRUE){
        OrnamentHeight = 1;
    }
    parent->frame.ornament_color1   = OrnamentColor1;
    parent->titlebar_ornament_color = OrnamentColor1;

    doFillWindow(
        tbWindow->absolute_x, 
        ( (tbWindow->absolute_y) + (tbWindow->height) - METRICS_TITLEBAR_ORNAMENT_SIZE ),  
        tbWindow->width, 
        OrnamentHeight, 
        OrnamentColor1, 
        0 );  // rop_flags no rop in this case?

//----------------------
// String
// Titlebar string support.
// Using the parent's name.

    int useTitleString = has_string;  //#HACK
    unsigned long StringLeftPad = 0;
    unsigned long StringTopPad = 8;  // char size.
    size_t StringSize = (size_t) strlen( (const char *) parent->name );
    if (StringSize > 64){
        StringSize = 64;
    }

// pad | icon | pad | pad
    if (useIcon == FALSE){
        StringLeftPad = (unsigned long) METRICS_ICON_LEFTPAD;
    }
    if (useIcon == TRUE){
        StringLeftPad = 
            (unsigned long) ( METRICS_ICON_LEFTPAD +iWidth +(2*METRICS_ICON_LEFTPAD));
    }

//
// Text support
//
    // #todo
    // We already did that before.
    parent->titlebar_text_color = 
        (unsigned int) get_color(csiTitleBarTextColor);

// #todo
// Temos que gerenciar o posicionamento da string.
// #bugbug: Use 'const char *'

    tbWindow->name = (char *) strdup( (const char *) parent->name );
    if ((void*) tbWindow->name == NULL){
        printf("do_create_titlebar: Invalid name\n");
        return NULL;
    }

    unsigned long sL=0;
    unsigned long sT=0;
    unsigned int sColor = 
        (unsigned int) parent->titlebar_text_color;
    if (useTitleString == TRUE)
    {
        // Saving relative position.
        parent->titlebar_text_left = StringLeftPad;
        parent->titlebar_text_top = StringTopPad;
        sL = (unsigned long) ((tbWindow->absolute_x) + StringLeftPad);
        sT = (unsigned long) ((tbWindow->absolute_y) + StringTopPad);
        grDrawString ( sL, sT, sColor, tbWindow->name );
    }

// ---------------------------------
// Controls
    do_create_controls(tbWindow);

//----------------------
    parent->titlebar = (struct gws_window_d *) tbWindow;  // Window pointer!

    return (struct gws_window_d *) tbWindow;
}


/*
 * wmCreateWindowFrame:
 */
// Called by CreateWindow in createw.c
// #importante:
// Essa rotina será chamada depois que criarmos uma janela básica,
// mas só para alguns tipos de janelas, pois nem todos os tipos 
// precisam de um frame. Ou ainda, cada tipo de janela tem um 
// frame diferente. Por exemplo: Podemos considerar que um checkbox 
// tem um tipo de frame.
// Toda janela criada pode ter um frame.
// Durante a rotina de criação do frame para uma janela que ja existe
// podemos chamar a rotina de criação da caption bar, que vai criar os
// botões de controle ... mas nem toda janela que tem frame precisa
// de uma caption bar (Title bar).
// Estilo do frame:
// Dependendo do estilo do frame, podemos ou nao criar a caption bar.
// Por exemplo: Uma editbox tem um frame mas não tem uma caption bar.
// IN:
// parent = parent window ??
// window = The window where to build the frame.
// x
// y
// width
// height
// style = Estilo do frame.
// OUT:
// 0   = ok, no erros;
// < 0 = not ok. something is wrong.

int 
wmCreateWindowFrame ( 
    struct gws_window_d *parent,
    struct gws_window_d *window,
    unsigned long border_size,
    unsigned int border_color1,
    unsigned int border_color2,
    unsigned int border_color3,
    unsigned int ornament_color1,
    unsigned int ornament_color2,
    int frame_style ) 
{
    int useFrame       = FALSE;
    int useTitleBar    = FALSE;
    int useTitleString = FALSE;
    int useIcon        = FALSE;
    int useStatusBar   = FALSE;
    int useBorder      = FALSE;
    // ...

// #bugbug
// os parâmetros 
// parent, 
// x,y,width,height 
// não estão sendo usados.

// Overlapped.
// Janela de aplicativos.

// Title bar and status bar.
    struct gws_window_d  *tbWindow;
    struct gws_window_d  *sbWindow;
    int id=-1;  //usado pra registrar janelas filhas.
    int Type=0;
// Border size
    unsigned long BorderSize = (border_size & 0xFFFF);
// Border color
    unsigned int BorderColor1 = border_color1;  // top/left
    unsigned int BorderColor2 = border_color2;  // right/bottom
    unsigned int BorderColor3 = border_color3;
// Ornament color
    unsigned int OrnamentColor1 = ornament_color1;
    unsigned int OrnamentColor2 = ornament_color2;
// Title bar height
    unsigned long TitleBarHeight = 
        METRICS_TITLEBAR_DEFAULT_HEIGHT;

// Titlebar color for active window.
    unsigned int TitleBarColor = 
        (unsigned int) get_color(csiActiveWindowTitleBar);

    int icon_id = ICON_ID_APP; //dfault.

    //unsigned long X = (x & 0xFFFF);
    //unsigned long Y = (y & 0xFFFF);
    //unsigned long Width = (width & 0xFFFF);
    //unsigned long Height = (height & 0xFFFF);

// #todo
// Se estamos minimizados ou a janela mãe está minimizada,
// então não temos o que pintar.
// #todo
// O estilo de frame é diferente se estamos em full screen ou maximizados.
// não teremos bordas laterais
// #todo
// Cada elemento da frame que incluimos, incrementa
// o w.top do retângulo da área de cliente.

// check parent
    if ( (void*) parent == NULL ){
        //gwssrv_debug_print ("wmCreateWindowFrame: [FAIL] parent\n");
        return -1;
    }
    if (parent->used != TRUE || parent->magic != 1234){
        return -1;
    }

// check window
    if ((void*) window == NULL){
        //gwssrv_debug_print ("wmCreateWindowFrame: [FAIL] window\n");
        return -1;
    }
    if (window->used != TRUE || window->magic != 1234){
        return -1;
    }

// Uma overlapped maximizada não tem borda.
    int IsMaximized = FALSE;
    if (window->style & WS_MAXIMIZED){
        IsMaximized=TRUE;
    }
// Uma overlapped maximizada não tem borda.
    int IsFullscreen = FALSE;
    if (window->style & WS_FULLSCREEN){
        IsFullscreen=TRUE;
    }

// #bugbug
// Estamos mascarando pois os valores anda corrompendo.
    window->absolute_x = (window->absolute_x & 0xFFFF);
    window->absolute_y = (window->absolute_y & 0xFFFF);
    window->width  = (window->width  & 0xFFFF);
    window->height = (window->height & 0xFFFF);

// #test:
// Defaults:
// Colocamos default, depois muda de acordo com os parametros.
    window->frame.titlebar_icon_id = ICON_ID_DEFAULT;
    // ...

// #todo
// Desenhar o frame e depois desenhar a barra de títulos
// caso esse estilo de frame precise de uma barra.
// Editbox
// EDITBOX NÃO PRECISA DE BARRA DE TÍTULOS.
// MAS PRECISA DE FRAME ... QUE SERÃO AS BORDAS.

// Type
// Qual é o tipo da janela em qual precisamos
// criar o frame. Isso indica o tipo de frame.

    Type = window->type;

    switch (Type){

    case WT_EDITBOX:
    case WT_EDITBOX_MULTIPLE_LINES:
        useFrame=TRUE; 
        useIcon=FALSE;
        useBorder=TRUE;
        break;

    // Uma overlapped maximizada não tem borda.
    case WT_OVERLAPPED:
        useFrame=TRUE; 
        useTitleBar=TRUE;  // Normalmente uma janela tem a barra de t[itulos.
        useTitleString=TRUE;
        useIcon=TRUE;
        useBorder=TRUE;
        // Quando a overlapped esta em fullscreen,
        // então não usamos title bar,
        // nem bordas.
        if (window->style & WS_FULLSCREEN)
        {
            //useFrame=FALSE;
            useTitleBar=FALSE;
            useTitleString=FALSE;
            useIcon=FALSE;
            useBorder=FALSE;
        }
        if (window->style & WS_STATUSBAR){
            useStatusBar=TRUE;
        }
        break;

    case WT_BUTTON:
    case WT_ICON:
        useFrame=TRUE;
        useIcon=FALSE;
        break;

    //default: break;
    };

    if (useFrame == FALSE){
        gwssrv_debug_print ("wmCreateWindowFrame: [ERROR] This type does not use a frame.\n");
        return -1;
    }

// ===============================================
// editbox

    if ( Type == WT_EDITBOX_SINGLE_LINE || 
         Type == WT_EDITBOX_MULTIPLE_LINES )
    {

        // #todo
        // The window structure has a element for border size
        // and a flag to indicate that border is used.
        // It also has a border style.

        // #todo: Essa rotina de cores deve ir para
        // dentro da função __draw_window_border().
        // ou passar tudo via argumento.
        // ou criar uma rotina para mudar as caracteristicas da borda.
         
        // Se tiver o foco.
        if (window->focus == TRUE){
            BorderColor1 = (unsigned int) get_color(csiWWFBorder);
            BorderColor2 = (unsigned int) get_color(csiWWFBorder);
            BorderSize = 2;  //#todo: worker
        }else{
            BorderColor1 = (unsigned int) get_color(csiWindowBorder);
            BorderColor2 = (unsigned int) get_color(csiWindowBorder);
            BorderSize = 1;  //#todo: worker
        };
        
        window->border_size = 0;
        window->borderUsed = FALSE;
        if (useBorder==TRUE){
            window->border_color1 = (unsigned int) BorderColor1;
            window->border_color2 = (unsigned int) BorderColor2;
            window->border_size = BorderSize;
            window->borderUsed = TRUE;
        }

        // Draw the border of an edit box.
        __draw_window_border(parent,window);
        return 0;
    }

// ===============================================
// Overlapped?
// Draw border, titlebar and status bar.
// #todo:
// String right não pode ser maior que 'last left' button.

    if (Type == WT_OVERLAPPED)
    {
        // #todo
        // Maybe we nned border size and padding size.
        
        // Consistente para overlapped.
        //BorderSize = METRICS_BORDER_SIZE;
        // ...
        
        // #todo
        // The window structure has a element for border size
        // and a flag to indicate that border is used.
        // It also has a border style.

        // Se tiver o foco.
        //if (window->focus == TRUE){
        //    BorderColor1 = (unsigned int) get_color(csiWWFBorder);
        //    BorderColor2 = (unsigned int) get_color(csiWWFBorder);
        //}else{
        //    BorderColor1 = (unsigned int) get_color(csiWindowBorder);
        //    BorderColor2 = (unsigned int) get_color(csiWindowBorder);
        //};

        //window->border_size = 0;
        //window->borderUsed = FALSE;
        //if (useBorder==TRUE){
            //window->border_color1 = (unsigned int) BorderColor1;
            //window->border_color2 = (unsigned int) BorderColor2;
            //window->border_size   = BorderSize;
        //    window->borderUsed    = TRUE;
        //}

        // Quatro bordas de uma janela overlapped.
        // Uma overlapped maximizada não tem bordas.
        window->borderUsed = FALSE;
        
        if ( IsMaximized == FALSE && 
             IsFullscreen == FALSE)
        {
            //WindowManager.is_fullscreen = TRUE;
            //WindowManager.fullscreen_window = window;
            
            window->borderUsed = FALSE;
            __draw_window_border(parent,window);
            // Now we have a border size.
        }

        // #important:
        // The border in an overlapped window will affect
        // the top position of the client area rectangle.
        window->rcClient.top += window->border_size;

        //
        // Title bar
        //

        // #todo
        // The window structure has a flag to indicate that
        // we are using titlebar.
        // It also has a title bar style.
        // Based on this style, we can setup some
        // ornaments for this title bar.
        // #todo
        // Simple title bar.
        // We're gonna have a wm inside the display server.
        // The title bar will be very simple.
        // We're gonna have a client area.
        // #bugbug
        // Isso vai depender da resolução da tela.
        // Um tamanho fixo pode fica muito fino em uma resolução alta
        // e muito largo em uma resolução muito baixa.
        
        // Title bar
        // Se a janela overlapped tem uma title bar.
        // #todo: Essa janela foi registrada?
        if (useTitleBar == TRUE)
        {
            // This is a application window.
            if ( window->style & WS_APP)
                icon_id = ICON_ID_APP;
            // This is a application window.
            if ( window->style & WS_DIALOG) 
                icon_id = ICON_ID_FILE;
            // This is a application window.
            if ( window->style & WS_TERMINAL) 
                icon_id = ICON_ID_TERMINAL;


            // IN: 
            // parent, border size, height, color, ornament color,
            //  use icon, use string.
            tbWindow = 
                (struct gws_window_d *) do_create_titlebar(
                    window,
                    TitleBarHeight,
                    TitleBarColor,
                    OrnamentColor1,
                    useIcon,
                    icon_id,
                    useTitleString );

            // Register window
            id = RegisterWindow(tbWindow);
            if (id<0){
                gwssrv_debug_print ("wmCreateWindowFrame: Couldn't register window\n");
                return -1;
            }

            // #important:
            // The Titlebar in an overlapped window will affect
            // the top position of the client area rectangle.
            // Depois de pintarmos a titlebar,
            // temos que atualizar o top da área de cliente.
            window->rcClient.top += window->titlebar_height;
        }  //--use title bar.
        // ooooooooooooooooooooooooooooooooooooooooooooooo

        // #todo:
        // nessa hora podemos pintar a barra de menu, se o style
        // da janela assim quiser. Depois disso precisaremos
        // atualizar o top da área de cliente.
        //window->rcClient.top += window->titlebar_height;

        // Status bar
        // (In the bottom)
        // #todo: It turns the client area smaller.
        //if (window->style & WS_STATUSBAR)
        if (useStatusBar == TRUE)
        {
            //#debug
            //printf ("sb\n");
            //while(1){}

            // #todo
            // Move these variables to the start of the routine.
            unsigned long sbLeft=0;
            unsigned long sbTop=0;
            unsigned long sbWidth=8;
            unsigned long sbHeight=32;

            window->statusbar_height=sbHeight;

            unsigned int sbColor = COLOR_STATUSBAR4;
            window->statusbar_color = (unsigned int) sbColor;

            // ??
            // Se tem uma parent válida?
            // Porque depende da parent?
            //if ( (void*) window->parent != NULL )
            if ( (void*) window != NULL )
            {
                // Relative to the app window.
                sbTop = 
                (unsigned long) (window->rcClient.height - window->statusbar_height);
                // #bugbug
                // We're gonna fail if we use
                // the whole width 'window->width'.
                // Clipping?
                sbWidth = 
                (unsigned long) (window->width - 4);
            }

            // Estamos relativos à nossa área de cliente
            // Seja ela do tipo que for.
            // #todo: apos criarmos a janela de status no fim da
            // area de cliente, então precisamos redimensionar a
            // nossa área de cliente.
            
            // #debug
            //printf ("l=%d t=%d w=%d h=%d\n",
            //    sbLeft, sbTop, sbWidth, sbHeight );
            //while(1){}
            
            sbWindow = 
                (void *) doCreateWindow ( 
                             WT_SIMPLE, 
                             0, // Style 
                             1, 
                             1, 
                             "Statusbar", 
                             sbLeft, sbTop, sbWidth, sbHeight,
                             (struct gws_window_d *) window, 
                             0, 
                             window->statusbar_color,  //frame
                             window->statusbar_color,  //client
                             (unsigned long) window->rop );   // rop_flags  
            
            // Depois de pintarmos a status bar, caso o estilo exija,
            // então devemos atualizar a altura da área de cliente.
            window->rcClient.height -= window->statusbar_height;

            if ( (void *) sbWindow == NULL ){
                gwssrv_debug_print ("wmCreateWindowFrame: sbWindow fail \n");
                return -1;
            }
            sbWindow->type = WT_SIMPLE;
            sbWindow->isStatusBar = TRUE;
            window->statusbar = (struct gws_window_d *) sbWindow;  // Window pointer.
            // Register window
            id = RegisterWindow(tbWindow);
            if (id<0){
                gwssrv_debug_print ("wmCreateWindowFrame: Couldn't register window\n");
                return -1;
            }
        }

        // ok
        return 0;
    }

// ===============================================
// Icon

    if (Type == WT_ICON)
    {
        window->borderUsed = TRUE;
        __draw_window_border(parent,window);
        //printf("border\n"); while(1){}
        return 0;
    }

    return 0;
}


// Change the root window color and reboot.
void wm_reboot(void)
{

// #todo
// Create wm_shutdown()?

// Draw the root window using the desktop default color.
    if ((void*) __root_window != NULL)
    {
        if (__root_window->magic == 1234)
        {
            __root_window->bg_color = 
                (unsigned int) get_color(csiDesktop);
            redraw_window(__root_window,FALSE);
            // #todo
            // Print some message, draw some image, etc.
            printf("wm_reboot:\n");
            wm_flush_window(__root_window);
            
            // #todo
            // Free resources
        }
    }

// Destroy all the windows.
    DestroyAllWindows();
// Hw reboot.
    rtl_reboot();
    exit(0);  //paranoia
}

static void animate_window(struct gws_window_d *window)
{
    register int i=0;

    if ( (void*) window == __root_window){
        return;
    }
    if (window->magic!=1234){
        return;
    }
    
    for (i=0; i<800; i++)
    {
         if ( (window->absolute_x - 1) == 0){
             return;
         }
         if ( (window->absolute_y - 1) == 0){
             return;
         }
         gwssrv_change_window_position(
              window, 
              window->absolute_x -1, 
              window->absolute_y  -1);
              redraw_window(window,FALSE);
              invalidate_window(window);
    };
}


// Starting with the first_window of the list,
// create a stack of windows
// This is the zorder.
// #todo:
// only application windows? overlapped.

static void wm_tile(void)
{
// #todo
// Maybe we can receive some parameters.

    struct gws_window_d *w;
    int cnt=0;
    //int c=0;
    register int i=0;

// Nothing to do.

    if (CONFIG_USE_TILE != 1){
        return;
    }
    if (current_mode == GRAMADO_JAIL){
        return;
    }
    if (WindowManager.initialized != TRUE){
        return;
    }

// Start with the first_window of the list.
// zorder: The last window is on top of the zorder.

// =============================
// Get the size of the list.
    cnt=0;
    w = (struct gws_window_d *) first_window;
    if ((void*)w == NULL){
        debug_print("wm_tile: w==NULL\n");
        return;
    }
    while ((void*)w != NULL){
        w = (struct gws_window_d *) w->next;
        cnt++;
    };

// =============================
// Starting with the first window of the list,
// create a stack of windows in the top/left corner of the screen.
    w = (struct gws_window_d *) first_window;
    if ((void*) w == NULL){
        debug_print("wm_tile: w==NULL\n");
        return; 
    }
    //if(w->magic!=1234)
    //    return;

// #bugbug: 
// limite provisorio

    //if( cnt>4 ){
    //    cnt=4;
    //}

// Initializing

    // Working Area
    unsigned long Left   = WindowManager.wa.left;
    unsigned long Top    = WindowManager.wa.top;
    unsigned long Width  = WindowManager.wa.width;
    unsigned long Height = WindowManager.wa.height;

    // Window stuff
    unsigned long l2=0;
    unsigned long t2=0;
    unsigned long w2=0;
    unsigned long h2=0;

    i=0;

    if (cnt<=0){
        return;
    }

    while ((void*)w != NULL)
    {
        if (i >= cnt){
            break;
        }

        // Window manager in tiled mode.
        if (WindowManager.mode == WM_MODE_TILED)
        {
            // Horizontal
            if (WindowManager.vertical==FALSE)
            {
                // for titlebar color support.
                // not the active window.
                w->active = FALSE;
                w->focus = TRUE;
                w->border_size = 1;

                // resize.
                // width: Metade da largura da área de trabalho.
                // height: Altura da área de trabalho dividido pela
                // quantidade de janelas que temos.
                Width  = (unsigned long) (WindowManager.wa.width / 2) -4;
                Height = (unsigned long) WindowManager.wa.height;
                if (cnt > 1)
                    Height = (unsigned long) (WindowManager.wa.height / (cnt-1));

                w2 = Width;
                h2 = Height -4;
                gws_resize_window(w, w2, h2);

                // positions.
                // left: comaça na metade da área de tranalho.
                // top: depende do indice da janela na lista.
                Left = (unsigned long) (WindowManager.wa.width / 2) +2;
                Top  = (unsigned long) (Height * i);
                l2 = Left;
                t2 = Top +2;
                gwssrv_change_window_position(w, l2, t2);

                // master?
                // Se estivermos na última janela da lista,
                // então ela será a master.
                // ocupara toda a metade esquerda da área de trabalho.
                if (i == cnt-1)
                {
                    // for titlebar color support.
                    // the active window.
                    w->active = TRUE;
                    w->focus = TRUE;
                    w->border_size = 2;
                    keyboard_owner = (void*) w;
                    last_window    = (void*) w;
                    top_window     = (void*) w;  //z-order: top window.
                    
                    // resize.
                    // width: metade da área de trabalho.
                    // height: altura da área de trabalho.
                    Width  = (unsigned long) (WindowManager.wa.width / 2);
                    Height = (unsigned long) WindowManager.wa.height;
                    w2 = Width  -4;
                    h2 = Height -4;
                    gws_resize_window(w, w2, h2);

                    // positions.
                    // Canto superior esquerdo. Com padding.
                    Left = (unsigned long) WindowManager.wa.left;
                    Top  = (unsigned long) WindowManager.wa.top; 
                    l2 = Left +2;
                    t2 = Top  +2;
                    gwssrv_change_window_position(w, l2, t2);
                }
            }

            // Vertical.
            if (WindowManager.vertical==TRUE)
            {
                //#todo:
                //w->height = WindowManager.wa.height; 
                //w->width  = (WindowManager.wa.width/cnt);
                //w->left   = (w->width * i);
                //w->top    = 0;
            }
        }

        w = (struct gws_window_d *) w->next;
        i++;
    };
}

// Vamos gerenciar a janela de cliente
// recentemente criada.
// Somente janelas overlapped serao consideradas clientes
// por essa rotina.
// Isso sera chamado de dentro do serviço que cria janelas.
// OUT: 0 = ok | -1 = Fail
int wmBindWindowToClient(struct gws_window_d *w)
{
// Associa a estrutura de janela
// com uma estrutura de cliente. 

// #todo
// Change the name of this function.
// It's causing confusion.

    struct gws_client_d *c;
    struct gws_client_d *tmp;
    register int i=0;

    if ((void*) w == NULL){
        goto fail;
    }
    if (w->magic != 1234){
        goto fail;
    }
    if (w->type != WT_OVERLAPPED){
        goto fail;
    }
    if ((void*) first_client == NULL){
        goto fail;
    }

// Client structure
    c = (void *) malloc( sizeof( struct gws_client_d ) );
    if ( (void*) c == NULL ){
        goto fail;
    }

    c->l = w->absolute_x;
    c->t = w->absolute_y;
    c->w = w->width;
    c->h = w->height;
    for (i=0; i<4; i++){
        c->tags[i] = TRUE;
    };
    c->pid = w->client_pid;
    c->tid = w->client_tid;
    c->used = TRUE;
    c->magic = 1234;

// Insert it into the list.

    tmp = (struct gws_client_d *) first_client;
    if ((void*) tmp == NULL){
        goto fail;
    }
    if (tmp->magic != 1234){
        goto fail;
    }

    while (1){
        // Found
        if ((void*) tmp->next == NULL){
            break;
        }
        // Next
        tmp = (struct gws_client_d *) tmp->next; 
    };

    if (tmp->magic != 1234){
        goto fail;
    }

    tmp->next = (struct gws_client_d *) c;
    return 0;

fail:
    yellow_status("wmBindWindowToClient");
    printf("wmBindWindowToClient: fail\n");
    return (int) (-1);
}

// Repinta todas as janelas seguindo a ordem da lista
// que está em last_window.
// No teste isso é chamado pelo kernel através do handler.
// Mas também será usado por rotinas internas.
void wm_update_desktop(int tile, int show)
{
    struct gws_window_d *w;  // tmp
    struct gws_window_d *l;  // last of the stack

// #test
// Starting with the first window of the list,
// create a stack o windows in the top/left corner of the screen.
// #todo: 
// Maybe we use an argument here. A set of flags.
    if (tile)
    {
        if (WindowManager.mode == WM_MODE_TILED){
            wm_tile();
        }
    }

// Redraw root window, but do not shot it yet.
    redraw_window(__root_window,FALSE);

// #test
// Testing zoom.
// ======================================

/*
 // #ok: It's working
    bmp_decode_system_icon0(
        4,  //Index
        8,  // x
        8,  // y
        TRUE,  // show
        4 // zoom factor
        );
    //refresh_screen();
    while(1){}
*/

// ======================================
// Redraw the whole stack of windows,
// but do not show them yet.
// Only for app windows. (>>> OVERLAPPED <<<).
// Set the last window in the stack as the active window.
// Set focus on the last window of the stack. 

    w = (struct gws_window_d *) first_window;
    if ((void*)w == NULL)
    {
        first_window = NULL;
        wm_Update_TaskBar("DESKTOP",FALSE);
        flush_window(__root_window);
        return;
    }
    if (w->magic != 1234)
    {
        first_window = NULL;
        wm_Update_TaskBar("DESKTOP",FALSE);
        flush_window(__root_window);
        return;
    }

// The first is the last valid window.
    l = (struct gws_window_d *) w;

// Loop to redraw the linked list.
    while (1){

        if ((void*)w==NULL){ 
            break; 
        }

        if ((void*) w != NULL)
        {
            // Only overlapped windows.
            if (w->type == WT_OVERLAPPED)
            {
                // This is the last valid for now.
                l = (struct gws_window_d *) w;
                // Redraw, but do no show it.
                redraw_window(w,FALSE);

                // Post message to the main window.
                // Paint the childs of the 'window with focus'.
                //on_update_window(w,GWS_Paint);
                //invalidate_window(w);
            }
        }

        w = (struct gws_window_d *) w->next; 
    }; 

// Set focus on last valid. Starting at first one.
// Activate
    set_active_window(l);
    //set_focus(l);  //no ... focus on client window.

// Update the taskbar at the bottom of the screen,
// but do not show it yet.
// Print the name of the active window.
    char *aw_name;
    //wm_Update_TaskBar("DESKTOP",FALSE);
    if ((void*) l != NULL)
    {
        if (l->magic == 1234)
        {
            if ((void*) l->name != NULL)
            {
                aw_name = l->name;
                wm_Update_TaskBar(aw_name,FALSE);
            }
        }
    }

// Invalidate the root window.
// Shows the whole screen
    //invalidate_window(__root_window);
    if (show){
        flush_window(__root_window);
    }


// #test
// #debug
// envia paint pra todo mundo.
// naquela janela assim saberemos quais janelas estao pegando input ainda.
// #bugbug: segunda mensagem de paint.
// >>> Isso eh muito legal.
//     pois atualiza todas janelas quando em tile mode
//     e mostra qual nao esta pegando eventos.
    window_post_message_broadcast( 
        0,           // wid = Ignored
        GWS_Paint,   // msg = msg code
        0,        // long1 = 
        0 );      // long2 = 
}

void wm_update_active_window(void)
{
    int wid = -1;
    if ((void*) active_window == NULL){
        return;
    }
    if (active_window->magic != 1234){
        return;
    }
    wid = (int) active_window->id;
    wm_update_window_by_id(wid);
}

// Set the last widnow and update the desktop.
void 
wm_update_desktop2(
    struct gws_window_d *last_window,
    int tile )
{
    set_last_window(last_window);
    wm_update_desktop(TRUE,TRUE);
}

void wm_update_desktop3(struct gws_window_d *new_top_window)
{
// #todo
// We need to redraw a lot of windows.


// Root
    if ((void*) __root_window == NULL)
        return;
    redraw_window(__root_window,FALSE);

//
// The new 'top window'.
//

    if ((void*) new_top_window == NULL)
        return;
    if (new_top_window->magic != 1234)
        return;

    top_window = new_top_window;

    set_active_window(top_window);
    //set_focus(top_window); //#wrong: The focus goes to the child.
    redraw_window(top_window,FALSE);
    // Post message to the main window.
    // Paint the childs of the window with focus.
    on_update_window(top_window,GWS_Paint);


//
// String
//

// Show the name of the top window 
// into the taskbar.

    char *tw_name;
    //wm_Update_TaskBar("...",FALSE);
    if ((void*) top_window != NULL)
    {
        if (top_window->magic == 1234)
        {
            if ((void*) top_window->name != NULL)
            {
                tw_name = top_window->name;
                wm_Update_TaskBar(tw_name,FALSE);
            }
        }
    }

    if ((void*) top_window == NULL)
        wm_Update_TaskBar("...",FALSE);

// Flush the whole desktop.
    flush_window(__root_window);
}

// #todo
// Explain it better.
void wm_update_window_by_id(int wid)
{
    struct gws_window_d *w;
    unsigned long fullWidth = 0;
    unsigned long fullHeight = 0;

// Redraw and show the root window.
    //redraw_window(__root_window,TRUE);

// wid
    if (wid<0){
        return;
    }
    if (wid>=WINDOW_COUNT_MAX){
        return;
    }

// Window structure
    w = (struct gws_window_d *) windowList[wid];
    if ((void*)w==NULL)  { return; }
    if (w->used != TRUE) { return; }
    if (w->magic != 1234){ return; }

    if (w->type != WT_OVERLAPPED){
        return;
    }

// #test
// Empilhando verticalmente.
    if (WindowManager.initialized != TRUE){
        return;
    }

// Tiled mode.
// Esses metodos irao atualizar tambem os valores da barra de titulos.
    if (WindowManager.mode == WM_MODE_TILED){
        gwssrv_change_window_position(w,0,0);
        gws_resize_window(
            w,
            WindowManager.wa.width,
            WindowManager.wa.height);
    }

    if (WindowManager.is_fullscreen == TRUE)
    {
        // #test
        // for titlebar color support.
        // the active window.
        w->active = TRUE;
        w->focus = TRUE;
        w->border_size = 2;
        keyboard_owner = (void*) w;
        last_window    = (void*) w;
        top_window     = (void*) w;  //z-order: top window.

        fullWidth  = gws_get_device_width();
        fullHeight = gws_get_device_height();
        gwssrv_change_window_position(w,0,0);
        gws_resize_window(
            w,
            fullWidth,
            fullHeight);
    }

    //keyboard_owner = (void *) w;
    //last_window    = (void *) w;
    //top_window     = (void *) w;

    set_active_window(w);
    //set_focus(w);

    redraw_window(w,FALSE);
    invalidate_window(w);

// Paint the childs of the window with focus.
    on_update_window(w,GWS_Paint);

//#todo: string
    //wm_Update_TaskBar("Win",TRUE);
}

/*
//#todo
struct gws_window_d *get_active(void);
struct gws_window_d *get_active(void)
{
}
*/

// Set the foreground thread given its tid.
// Pede para o kernel mudar a foreground thread.
// A foreground thread será a thread associada com a janela
// que possui o foco de entrada.
void __set_foreground_tid(int tid)
{
    if (tid<0){
        return;
    }
    sc82 ( 10011, tid, tid, tid );
}

// Set the keyboard_owner window.
void set_focus(struct gws_window_d *window)
{
    int tid = -1;
    struct gws_window_d *old_owner;

    if ((void*) window == NULL){
        return;
    }
    if (window->used != TRUE){
        return;
    }
    if (window->magic != 1234){
        return;
    }

// We alredy have the focus.
    if (window == keyboard_owner)
        return;

// Save
    old_owner = keyboard_owner;
// Set
    //keyboard_owner = (void*) window;

// Is it an editbox?
// Redraw it with a new style.
// This routine need to know if we're the keyboard owner
// to select the style.
// IN: window, show
    if ( window->type == WT_EDITBOX || 
         window->type == WT_EDITBOX_MULTIPLE_LINES )
    {
        keyboard_owner = (void*) window;
        // Repaint the new owner that has the focus.
        redraw_window(window,TRUE);
        // Repaint the old owner that has not the focus.
        if ( (void*) old_owner != NULL )
        {
            if (old_owner->magic == 1234)
            {
                if ( old_owner->type == WT_EDITBOX ||
                     old_owner->type == WT_EDITBOX_MULTIPLE_LINES )
                {
                    redraw_window(old_owner,TRUE);
                }
            }
        }

        // Set the foreground thread.
        // That's the tid associated with this window.
        tid = (int) window->client_tid;
        if (tid<0)
            return;
        __set_foreground_tid(tid);
    }

/*
// Set the foreground thread.
// That's the tid associated with this window.
    tid = (int) window->client_tid;
    if (tid<0)
        return;
    __set_foreground_tid(tid);
*/
}

// Get the keyboard_owner window.
// Pega o ponteiro da janela com foco de entrada.
struct gws_window_d *get_focus(void)
{
    return (struct gws_window_d *) get_window_with_focus();
}

// O mouse está sobre essa janela.
void set_mouseover(struct gws_window_d *window)
{
    if ( (void*) window == NULL ){
        return;
    }
    if (window->used != TRUE) { return; }
    if (window->magic != 1234){ return; }

// O mouse está sobre essa janela.
    mouse_hover = (void*) window;
}

// Pega o ponteiro da janela que o mouse esta sobre ela.
struct gws_window_d *get_mousehover(void)
{
    struct gws_window_d *w;

    w = (struct gws_window_d *) mouse_hover;
    if ( (void*)w==NULL ){
        return NULL;
    }
    if (w->used!=TRUE) { return NULL; }
    if (w->magic!=1234){ return NULL; }

    return (struct gws_window_d *) w; 
}


void set_status_by_id( int wid, int status )
{
    struct gws_window_d *w;

// wid
    if (wid<0){
        return;
    }
    if (wid>=WINDOW_COUNT_MAX){
        return;
    }
// Window structure
    w = (struct gws_window_d *) windowList[wid];
    if ((void*)w==NULL){
        return;
    }
    if (w->used != TRUE) { return; }
    if (w->magic != 1234){ return; }

// Set status
    w->status = (int) status;
}

void set_bg_color_by_id( int wid, unsigned int color )
{
    struct gws_window_d *w;

// wid
    if (wid<0){
        return;
    }
    if (wid>=WINDOW_COUNT_MAX){
        return;
    }
// Window structure
    w = (struct gws_window_d *) windowList[wid];
    if ((void*)w==NULL){
        return;
    }
    if (w->used != TRUE) { return; }
    if (w->magic != 1234){ return; }

// Set bg color
    w->bg_color = (unsigned int) color;
}

void set_clientrect_bg_color_by_id( int wid, unsigned int color )
{
    struct gws_window_d *w;

// wid
    if (wid<0){
        return;
    }
    if (wid>=WINDOW_COUNT_MAX){
        return;
    }
// Window structure
    w = (struct gws_window_d *) windowList[wid];
    if ((void*)w == NULL){
        return;
    }
    if (w->used != TRUE) { return; }
    if (w->magic != 1234){ return; }

// Set client rect bg color
    w->clientarea_bg_color = (unsigned int) color;
}

void set_focus_by_id(int wid)
{
    struct gws_window_d *w;

// wid
    if (wid<0){
        return;
    }
    if (wid >= WINDOW_COUNT_MAX){
        return;
    }

// Window structure
    w = (struct gws_window_d *) windowList[wid];
    if ((void*)w==NULL){
        return;
    }
    if (w->used != TRUE) { return; }
    if (w->magic != 1234){ return; }

    set_focus(w);
}

void set_active_by_id(int wid)
{
    struct gws_window_d *w;

// wid
    if (wid<0){
        return;
    }
    if (wid >= WINDOW_COUNT_MAX){
        return;
    }

// Window structure
    w = (struct gws_window_d *) windowList[wid];
    if ((void*)w==NULL){
        return;
    }
    if (w->used != TRUE) { return; }
    if (w->magic != 1234){ return; }

    set_active_window(w);
}


void set_first_window( struct gws_window_d *window)
{
    first_window = (struct gws_window_d *) window;
}

struct gws_window_d *get_first_window(void)
{
    return (struct gws_window_d *) first_window;
}

void set_last_window(struct gws_window_d *window)
{
    if ( (void*) window == NULL ){
         return;
    }
    if (window->magic != 1234){
        return;
    }
    wm_add_window_into_the_list(window);
}

struct gws_window_d *get_last_window(void)
{
    return (struct gws_window_d *) last_window;
}

void activate_first_window(void)
{
// Structure validation
    if ( (void*) first_window == NULL ){
        return;
    }
    if (first_window->used != TRUE){
        return;
    }
    if (first_window->magic != 1234){
        return;
    }
// Type validation
    if (first_window->type != WT_OVERLAPPED){
        return;
    }
// Set
    set_active_window(first_window);
}

void activate_last_window(void)
{
// Structure validation
    if ( (void*) last_window == NULL ){
        return;
    }
    if (last_window->used != TRUE) { return; }
    if (last_window->magic != 1234){ return; }

// Type validation
// #bugbug
// Can we active the root window?
// The root window is WT_SIMPLE.

    if (last_window->type != WT_OVERLAPPED){
        return;
    }
// Set
    set_active_window(last_window);
}

// The list starts with first_window.
void wm_add_window_into_the_list(struct gws_window_d *window)
{
    struct gws_window_d  *Next;

// ========================
    //if( window == __root_window )
        //return;
// ========================

// Structure validation
    if ((void*) window == NULL){
        return;
    }
    if (window->used != TRUE){
        return;
    }
    if (window->magic != 1234){
        return;
    }
// Type validation
    if (window->type != WT_OVERLAPPED){
        return;
    }

// =====================================
// Se não existe uma 'primeira da fila'.
// Então somos a primeira e a última.
    if ((void*) first_window == NULL)
    {
        first_window = window;
        last_window = window;
        goto done;
    }
// Invalid first window.
    if ( first_window->used != TRUE )
    {
        first_window = window;
        last_window = window;
        goto done;
    }
    if ( first_window->magic != 1234 )
    {
        first_window = window;
        last_window = window;
        goto done;
    }

// ===================================
// Se exite uma 'primeira da fila'.
    Next = first_window;
    while ( (void*) Next->next != NULL )
    {
        Next = Next->next;
    };

// Agora somos a última da fila.
    Next->next  = (struct gws_window_d *) window;

done:
    last_window = (struct gws_window_d *) window;
    window->next = NULL;
    set_active_window(window);
}

void wm_rebuild_list(void)
{
    struct gws_window_d *window;
    register int i=0;
    for (i=0; i<WINDOW_COUNT_MAX; i++)
    {
        window = (struct gws_window_d *) windowList[i];
        if ((void*) window != NULL)
        {
            if (window->magic == 1234)
            {
                if (window->type == WT_OVERLAPPED){
                    wm_add_window_into_the_list(window);
                }
            }
        }
    };
}


// not tested yet
void wm_remove_window_from_list_and_kill( struct gws_window_d *window)
{
    struct gws_window_d *w;
    struct gws_window_d *pick_this_one;

    if ( (void*) window == NULL ){
        return;
    }

    w = (struct gws_window_d *) first_window;
    if ( (void*) w == NULL ){
        return;
    }

    while(1)
    {
        if ( (void*) w == NULL ){
            break;
        }

        if (w == window)
        {
            // Remove
            pick_this_one = (struct gws_window_d *) w;
            // Glue the list.
            w = w->next;
            // Kill
            pick_this_one->used = FALSE;
            pick_this_one->magic = 0;
            pick_this_one = NULL;
            break;
        }

        w = w->next;
    };
}

// ====================

/*
DEC	HEX	CHARACTER
0	0	NULL
1	1	START OF HEADING (SOH)
2	2	START OF TEXT (STX)
3	3	END OF TEXT (ETX)
4	4	END OF TRANSMISSION (EOT)
5	5	END OF QUERY (ENQ)
6	6	ACKNOWLEDGE (ACK)
7	7	BEEP (BEL)
8	8	BACKSPACE (BS)
9	9	HORIZONTAL TAB (HT)
10	A	LINE FEED (LF)
11	B	VERTICAL TAB (VT)
12	C	FF (FORM FEED)
13	D	CR (CARRIAGE RETURN)
14	E	SO (SHIFT OUT)
15	F	SI (SHIFT IN)
16	10	DATA LINK ESCAPE (DLE)
17	11	DEVICE CONTROL 1 (DC1)
18	12	DEVICE CONTROL 2 (DC2)
19	13	DEVICE CONTROL 3 (DC3)
20	14	DEVICE CONTROL 4 (DC4)
21	15	NEGATIVE ACKNOWLEDGEMENT (NAK)
22	16	SYNCHRONIZE (SYN)
23	17	END OF TRANSMISSION BLOCK (ETB)
24	18	CANCEL (CAN)
25	19	END OF MEDIUM (EM)
26	1A	SUBSTITUTE (SUB)
27	1B	ESCAPE (ESC)
28	1C	FILE SEPARATOR (FS) RIGHT ARROW
29	1D	GROUP SEPARATOR (GS) LEFT ARROW
30	1E	RECORD SEPARATOR (RS) UP ARROW
31	1F	UNIT SEPARATOR (US) DOWN ARROW
*/

void 
wm_draw_char_into_the_window(
    struct gws_window_d *window, 
    int ch,
    unsigned int color )
{
// We are painting only on 'editbox'.
// Not on root window.
// #todo
// In the case of editbox, we need to put the text into a buffer
// that belongs to the window. This way the client application
// can grab this texts via request.


// draw char support.
    unsigned char _string[4];
// Vamos checar se é um controle ou outro tipo de char.
    unsigned char ascii = (unsigned char) ch;
    int is_control=FALSE;

// Invalid window
    if ((void*)window == NULL){
        return;
    }
    if (window->magic != 1234){
        return;
    }

// Not on root window.
    if (window == __root_window)
        return;

// Invalid window type
    int is_valid_wt=FALSE;
    switch (window->type){
        case WT_EDITBOX_SINGLE_LINE:
        case WT_EDITBOX_MULTIPLE_LINES:
            is_valid_wt = TRUE;
            break;
    };
    if (is_valid_wt != TRUE)
        return;


// Invalid char
    if (ch<0){
        return;
    }

// No enter
    if (ch == VK_RETURN)
        return;

    //#debug
    //if(ascii == 'M'){
    //    printf("M: %d\n",ascii);
    //}

/*
// #bugbug
// Com essa rotina ficamos impedidos de imprimirmos
// algumas letras maiúsculas, pois elas possuem o mesmo
// scancode que esses arrows.
// UP, LEFT, RIGHT, DOWN
// #todo
// Update input pointer for this window.
    if( ch==0x48 || 
        ch==0x4B || 
        ch==0x4D || 
        ch==0x50 )
    {
        // #todo: 
        // Update input pointers for this window.
        // right
        if(ch==0x4D){ window->ip_x++; }
        // down
        if(ch==0x50){ window->ip_y++; }
        return;
    }
*/

// Backspace
// (control=0x0E)
// #todo: 
// Isso tem que voltar apagando.
    if (ch == VK_BACK)
    {
        if (window->ip_x > 0){
            window->ip_x--;
        }

        if (window->ip_x == 0)
        {
            if (window->type == WT_EDITBOX_SINGLE_LINE)
            {
                window->ip_x = 0;
                return;
            }
            if (window->type == WT_EDITBOX_MULTIPLE_LINES)
            {
                if (window->ip_y > 0){
                    window->ip_y--;
                }
                
                if (window->ip_y == 0)
                {
                    window->ip_y=0;
                }
                
                // #todo #bugbug
                // Não é pra voltar no fim da linha anterior,
                // e sim no fim do texto da linha anterior.
                if (window->ip_y > 0){
                    window->ip_x = (window->width_in_chars -1);
                }
                return;
            }
        }
        return;
    }

// TAB
// (control=0x0F)
// O ALT esta pressionado?
    if (ch == VK_TAB)
    {
        window->ip_x += 8;
        if (window->ip_x >= window->width_in_chars)
        {
            if (window->type == WT_EDITBOX_SINGLE_LINE)
            {
                window->ip_x = (window->width_in_chars-1);
            }
            if (window->type == WT_EDITBOX_MULTIPLE_LINES)
            {
                window->ip_x = 0;
                window->ip_y++;
                if (window->ip_y >= window->height_in_chars)
                {
                    window->ip_y = (window->height_in_chars-1);
                }
            }
        }
        return;
    }

// ----------------------

// Not printable.
// 32~127
// A=41h | a=61H
// Control character or non-printing character (NPC).
// see:
// https://en.wikipedia.org/wiki/Control_character
// https://en.wikipedia.org/wiki/ASCII#Printable_characters
//    ASCII code 96  = ` ( Grave accent )
//    ASCII code 239 = ´ ( Acute accent )
//    ASCII code 128 = Ç ( Majuscule C-cedilla )
//    ASCII code 135 = ç ( Minuscule c-cedilla )
// 168 - trema

    int is_abnt2_printable=FALSE;

    // Not printable for US.
    if (ascii < 0x20 || ascii >= 0x7F)
    {
        // Control char
        if (ascii < 0x20 || ascii == 0x7F){
            is_control = TRUE;
        }
        
        if ( ascii == 168 ||   // trema
             ascii == 239 ||   // acute
             ascii == 128 ||   // Ç
             ascii == 135 )    // ç
        {
            is_abnt2_printable = TRUE;
            goto printable;
        }
        return;
    }


printable:

// string
   _string[0] = (unsigned char) ch;
   _string[1] = 0;

// types
    if (window->type == WT_OVERLAPPED){ return; }
    if (window->type == WT_SCROLLBAR) { return; }
    if (window->type == WT_STATUSBAR) { return; }
    if (window->type == WT_CHECKBOX)  { return; }
    if (window->type == WT_BUTTON)    { return; }
    // ...

// #todo
// Isso pode receber char se tiver em modo de edição.
// Para editarmos a label.
// #todo: edit label if in edition mode.
// #todo: open application if its a desktop icon.
    if (window->type == WT_ICON){
        return;
    }

// Editbox
// Printable chars.
// Print the char into an window 
// of type Editbox.
// Ascci printable chars: (0x20~0x7F)
// Terry's font has more printable chars.

    if ( window->type == WT_EDITBOX ||
         window->type == WT_EDITBOX_MULTIPLE_LINES )
    {
        // #todo
        // Devemos enfileirar os chars dentro de um buffer
        // indicado na estrutura de janela.
        // Depois podemos manipular o texto, inclusive,
        // entregarmos ele para o aplicativo. 
        
        // Draw char
        // #bugbug: Maybe we need to use draw_char??();
        // see: dtext.c
        //if(ascii=='M'){printf("M: calling dtextDrawText\n");}
        dtextDrawText ( 
            (struct gws_window_d *) window,
            (window->ip_x*8), 
            (window->ip_y*8), 
            (unsigned int) color, 
            (unsigned char *) _string );  //&_string[0] );

        // Refresh rectangle
        // x,y,w,h
        gws_refresh_rectangle ( 
            (window->absolute_x + (window->ip_x*8)), 
            (window->absolute_y + (window->ip_y*8)), 
            8, 
            8 );

        // Increment pointer.
        // Se for maior que a quantidade de bytes (chars?) na janela.
        window->ip_x++;
        if (window->ip_x >= window->width_in_chars)
        {
            window->ip_x=0;
            if (window->type == WT_EDITBOX_MULTIPLE_LINES)
            {    
                window->ip_y++;
                // Última linha?
                //if( window->ip_y > window->height_in_chars)
                //     fail!
            }
        }
    }
}


// #test: 
void __switch_active_window(int active_first)
{
// #todo
// #test: 
// Probe the window list and set the next
// when we found the active, if it is not NULL.

    struct gws_window_d *w;
    register int i=0;
    w = first_window;

    // Update zOrder.
    while (1){
        last_window = w;
        if (w == NULL)
            break;
        if (w->magic != 1234)
            break;
        if (w->type != WT_OVERLAPPED)
            break;
        // Update zIndex
        w->zIndex = (int) i;           
        // Next
        w = w->next;
    };

    if (active_first == TRUE){
        set_active_window(first_window);
        redraw_window(first_window,TRUE);
        on_update_window(first_window,GWS_Paint);
    }else{
        set_active_window(last_window);
        redraw_window(last_window,TRUE);
        on_update_window(last_window,GWS_Paint);
    };
}


// Post message:
// Colocaremos uma mensagem na fila de mensagens
// da thread associada com a janela indicada via argumento.
// Coloca em tail.

int
wmPostMessage(
    struct gws_window_d *window,
    int msg,
    unsigned long long1,
    unsigned long long2 )
{
// Post message to the thread.

    unsigned long message_buffer[8];

// Structure validation
    if ( (void*) window == NULL ){
        return -1;
    }
    if (window->used != TRUE) { return -1; }
    if (window->magic != 1234){ return -1; }

// No messages to root window.
    if (window == __root_window)
        return -1;

// Message code validation
    if (msg<0){
        return -1;
    }

// standard
// wid, msg code, data1, data2
    message_buffer[0] = (unsigned long) (window->id & 0xFFFF);
    message_buffer[1] = (unsigned long) (msg        & 0xFFFF);
    message_buffer[2] = (unsigned long) long1;
    message_buffer[3] = (unsigned long) long2;
// extra
    //message_buffer[4] = 0;
    //message_buffer[5] = 0;

// Invalid client tid.
    if (window->client_tid < 0){
        return -1;
    }
// receiver
    message_buffer[4] = 
        (unsigned long) (window->client_tid & 0xFFFF);
// sender
    message_buffer[5] = 0; //?

    int ClientTID = 
        (int) window->client_tid;
    
    unsigned long MessageBuffer = 
        (unsigned long) &message_buffer[0];

//
// Post
//

    if (ClientTID < 0)
        return -1;

// New foreground thread.
// Pede para o kernel mudar a foreground thread.
// Seleciona o próximo 'input reponder'.
// Assim o kernel colocará as próximas mensagens
// na fila dessa thread.

    // #bugbug
    // Somente a rotina de set_focus() vai vazer isso.
    //sc82 ( 10011, ClientTID, ClientTID, ClientTID );
    //__set_foreground_tid( 10011, ClientTID, ClientTID, ClientTID )

// Post message to a given thread.
// Add the message into the queue. In tail.
// IN: tid, message buffer address
// ?? Podemos mandar qualquer tipo de mensagem?
    rtl_post_system_message( 
        (int) ClientTID, (unsigned long) MessageBuffer );

    return 0;
}


/*
#deprecated
// Se o mouse esta passando sobre os botoes
// da barra de tarefas.
void __probe_tb_button_hover(unsigned long long1, unsigned long long2);
void __probe_tb_button_hover(unsigned long long1, unsigned long long2)
{
// Probe taskbar buttons.
// Well. Not valid in fullscreen.

    int Status=0;
    register int i=0;
    int max=4; // We have 4 buttons in the taskbar.
    struct gws_window_d *w;  // The window for a button.

    if (WindowManager.initialized!=TRUE){
        return;
    }
    if (WindowManager.is_fullscreen==TRUE){
        return;
    }

// Walk

    for (i=0; i<max; i++){

    // Get a pointer for a window.
    w = (struct gws_window_d *) tb_windows[i];
    // If this is a valid pointer.
    if ( (void*) w != NULL )
    {
        if (w->magic == 1234)
        {
            // Is the pointer inside this window?
            // Registra nova mouse_hover se estiver dentro.
            Status = is_within( (struct gws_window_d *) w, long1, long2 );
            if (Status==TRUE)
            {
                // set_mouseover(w);
                mouse_hover = (void*) w;
                return;
            }
        }
    }
    };
}
*/

// local
static void on_mouse_leave(struct gws_window_d *window)
{
// When the mouse pointer leaves a window.

    if ((void*) window == NULL)
        return;
    if (window->magic!=1234)
        return;

// Flag
    window->is_mouse_hover = FALSE;

// Update mouse pointer
    window->x_mouse_relative = 0;
    window->y_mouse_relative = 0;

// The old mousehover needs to comeback
// to the normal state.
// visual efect
    if (window->type == WT_BUTTON)
    {
        window->status = BS_DEFAULT;
        window->bg_color = (unsigned int) get_color(csiButton);
        redraw_window(window,TRUE);
    }
}

// local
static void on_mouse_hover(struct gws_window_d *window)
{

    if ((void*) window == NULL)
        return;
    if (window->magic!=1234)
        return;

// Flag
    window->is_mouse_hover = TRUE;

// visual efect
    if (window->type == WT_BUTTON)
    {
        window->status = BS_HOVER;
        // Using the color that belongs to this window.
        window->bg_color = 
            (unsigned int) window->bg_color_when_mousehover;
        redraw_window(window,TRUE);
    }

    // visual efect
    if ( window->type == WT_EDITBOX_SINGLE_LINE ||
         window->type == WT_EDITBOX_MULTIPLE_LINES )
    {
        // Do not redraw
        // Change the cursor type.
        // ...
    }
}

static void on_drop(void)
{
    struct gws_window_d *wgrab;

// Drop it
    grab_is_active = FALSE;  //
    is_dragging = FALSE;     // 

// Invalid wid.
    if ( grab_wid < 0 ||
         grab_wid >= WINDOW_COUNT_MAX )
    {
        return;
    }

// It needs to be an overlapped window.
    wgrab = (struct gws_window_d *) get_window_from_wid(grab_wid);
    if ((void*) wgrab == NULL)
        return;
    if (wgrab->magic != 1234)
        return;

// The grabbed window needs to be an Overlapped.         
    if (wgrab->type != WT_OVERLAPPED)
        return;

// Posição atual do mouse.
    unsigned long x=0;
    unsigned long y=0;
// Posiçao do mouse when dropping.
    unsigned long x_when_dropping = 0;
    unsigned long y_when_dropping = 0;

// Pega a posiçao atual.
    x = (unsigned long) comp_get_mouse_x_position();
    y = (unsigned long) comp_get_mouse_y_position();

// #bugbug
// Provisorio
    x_when_dropping = x;
    y_when_dropping = y;

// #test
// Using the relative pointer.
// (0,0)
// O posicionamento relativo para janelas overlapped
// nunca foram atualizados, portanto eh (0,0).

    //x_when_dropping = (unsigned long) wgrab->x_mouse_relative;
    //y_when_dropping = (unsigned long) wgrab->y_mouse_relative;

        
// #todo
// + Put the window in the top of the z-order.
// + Redraw all the desktop.
// + Activate the window.
// + Set focus?

    gwssrv_change_window_position( 
        wgrab, 
        x_when_dropping, 
        y_when_dropping );

// Redraw everything.
    wm_update_desktop3(wgrab);

// done:
    grab_wid = -1;
    return;
}

// local
// Se o mouse esta passando sobre alguma janela de alguns tipos.
void 
__probe_window_hover(
    unsigned long long1, 
    unsigned long long2 )
{
// Check if the mouse is hover a window, given the coordenates.
// Well. Not valid in fullscreen for now.

    int Status=0;
    register int i=0;
    int max = WINDOW_COUNT_MAX;  // All the windows in the global list.
    struct gws_window_d *w;
    struct gws_window_d *p;

    if (WindowManager.initialized!=TRUE){
        return;
    }
    if (WindowManager.is_fullscreen==TRUE){
        return;
    }

    //printf("long1=%d long2=%d\n",long1, long2);

// Walk

    for (i=0; i<max; i++){

    // Get a pointer for a window.
    w = (struct gws_window_d *) windowList[i];
    // If this is a valid pointer.
    if ( (void*) w != NULL )
    {
        if (w->magic == 1234)
        {
            // Valid types: (for now)
            if ( w->type == WT_EDITBOX_SINGLE_LINE ||
                 w->type == WT_EDITBOX_MULTIPLE_LINES ||
                 w->type == WT_BUTTON )
            {
                // Is the pointer inside this window?
                // Registra nova mouse_hover se estiver dentro.
                Status = is_within( (struct gws_window_d *) w, long1, long2 );
                if (Status==TRUE)
                {
                    // Deixe a antiga, e repinte ela,
                    // se estamos numa nova.
                    if (w != mouse_hover)
                    {
                        on_mouse_leave(mouse_hover);  // repinte a antiga
                        // The new mouse over.
                        // #todo: Create set_mouseover(w);
                        mouse_hover = (void*) w;      // se new
                        // Já que estamos numa nova, 
                        // vamos mudar o visual dela.
                        on_mouse_hover(w);            // repinte a nova
                    
                        // Update relative mouse pointer
                        w->x_mouse_relative = 
                           (unsigned long) (long1 - w->absolute_x);
                        w->y_mouse_relative = 
                           (unsigned long) (long2 - w->absolute_y);
                    }
                    return;
                }

                //#debug
                //printf ("Fora\n");
                //printf("x=%d y=%d | w->l=%d w->t=%d \n",
                //    long1, long2,
                //    w->left, w->top );
            }
            
            // #test
            // Are we hover a menu item?
            // We have two types of menuitens.
            // Titlebar is also SIMPLE.
            if (w->type == WT_SIMPLE)
            {
                if (w->isTitleBar == TRUE)
                {
                    Status = is_within( (struct gws_window_d *) w, long1, long2 );
                    // Yes, we are inside a menuitem.
                    if (Status==TRUE)
                    {
                        if (w != mouse_hover)
                        {
                            on_mouse_leave(mouse_hover);
                            mouse_hover = (void*) w;
                            on_mouse_hover(w);

                            // Update relative mouse pointer
                            w->x_mouse_relative = 
                               (unsigned long) (long1 - w->absolute_x);
                            w->y_mouse_relative = 
                               (unsigned long) (long2 - w->absolute_y);
                        }
                        return;
                    }
                }
            }

        }
    }
    };

// #test
// Assume root when no one was found
    //printf ("Not Found\n");

    on_mouse_leave(mouse_hover);  // repinte a antiga
    mouse_hover = (void*) __root_window;
    
}

unsigned long wmGetLastInputJiffie(int update)
{
    if (update==TRUE){
        last_input_jiffie = (unsigned long) rtl_jiffies();
    }
    return (unsigned long) last_input_jiffie;
}

// Read n bytes from stdin
int wmSTDINInputReader(void)
{
    size_t nreads=0;
    char buffer[512];
    register int i=0;

    nreads = (size_t) read(0,buffer,512);
    if (nreads<=0){
        return -1;
    }

    //printf("%d bytes\n",nreads);

    i=0;
    for (i=0; i<nreads; i++)
    {
            /*
            ?????procedure( 
                0,            // window pointer
                GWS_KeyDown,  // msg code
                buffer[i],    // long1
                buffer[i] );  // long2
            */
    };

    return (int) nreads;
}

void wmProcessMenuEvent(int event_number, int button_wid)
{

// Effect
// Mouse button released the start menu button
    if ( event_number == MENU_EVENT_RELEASED || 
         event_number == MENU_EVENT_COMBINATION )
    {
        if (button_wid == StartMenu.wid){
            __button_released(StartMenu.wid);
        }

        // Not created. Create!
        if (StartMenu.is_created != TRUE)
        {
            create_main_menu();
            return;
        }

        // Created, but not visible. Show it!
        if (StartMenu.is_visible != TRUE)
        {
            redraw_main_menu();
            return;
        }

        // Created and visible.
        // Update desktop but don't show the menu.
        if (StartMenu.is_visible == TRUE)
        {
            StartMenu.is_visible = FALSE;
            wm_update_desktop(TRUE,TRUE);
            return;
        }
    }
}

static int wmProcessCombinationEvent(int msg_code)
{
// Handle combination code.

    if (msg_code<0){
        goto fail;
    }

//
// z, x, c, v
//

// Control + z
// Undo
    if (msg_code == GWS_Undo)
    {
        printf("ws: undo\n");
        return 0;
    }

// Control + x
    if (msg_code == GWS_Cut)
    {
        printf("ws: cut\n"); 
        // #test
        // gwssrv_quit();
        //demoCat();

        __switch_active_window(TRUE);  //active first
        return 0;
    }

// Control + c
    if (msg_code == GWS_Copy)
    {
        printf("ws: copy\n");
        __switch_active_window(FALSE);  //active NOT FIRST
        return 0;
    }

// Control + v
    if (msg_code == GWS_Paste){
        printf("ws: paste\n");
        return 0;
    }

// [control + a]
// Select all.
// #test (ok)
// Post message to all the overlapped windows.
    if (msg_code == GWS_SelectAll)
    {
        printf("ws: select all\n");
        
        // #test:
        // Sending the wrong message.  
        // This is just a test for now.
        gwssrv_broadcast_close();
        return 0;
    }

// [control+f]
// Find
    if (msg_code == GWS_Find)
    {
        printf("ws: find\n");
        //printf("root: %s\n", WindowManager.root->name );
        //printf("taskbar: %s\n", WindowManager.taskbar->name );
        return 0;
    }


// Control + s
// #test
// Creates a menu for the root window.
// Only refresh if it is already created.
    if (msg_code == GWS_Save){
        wmProcessMenuEvent(MENU_EVENT_COMBINATION,-1);
        return 0;
    }

// --------------

// Control + Arrow keys.
    if (msg_code == GWS_ControlArrowUp){
        dock_active_window(1);
        return 0;
    }
    if (msg_code == GWS_ControlArrowRight){
        dock_active_window(2);
        return 0;
    }
    if (msg_code == GWS_ControlArrowDown){
        dock_active_window(3);
        return 0;
    }
    if (msg_code == GWS_ControlArrowLeft){
        dock_active_window(4); 
        return 0;
    }

// [shift + f12]
// Enable the ps2 mouse support
// by making the full ps2-initialization.
// Valid only for qemu.
// + Enable mouse.
// + Change bg color.
    if (msg_code == 88112)
    {
        // Calling the kernel to make the full ps2 initialization.
        // #todo: Create a wrapper fot that syscall.
        // #todo: Allow only the ws pid to make this call.
        sc82 ( 22011, 0, 0, 0 );
        // Enable the use of mouse here in the server.
        gUseMouse = TRUE;
        return 0;
    }

fail:
    return (int) (-1);
}

inline int is_combination(int msg_code)
{
    if (msg_code<0)
        return FALSE;

    switch (msg_code){
    case GWS_ControlArrowUp:
    case GWS_ControlArrowRight:
    case GWS_ControlArrowDown:
    case GWS_ControlArrowLeft:
    case GWS_Cut:
    case GWS_Copy:
    case GWS_Paste:
    case GWS_Undo:
    case GWS_SelectAll:
    case GWS_Find:
    case GWS_Save:
    case 88112:
        return TRUE;
        break;
    //...
    default:
        return FALSE;
        break;
    };

    return FALSE;
}

// wmInputReader:
// (Input port)
// Get the messages in the queue,
// respecting the circular queue.
int wmInputReader(void)
{
// + Get input events from the thread's event queue.
// + React to the events.
// Getting input events from the event queue
// inside the control thread structure.

    int status=0;

    register long i=0;
    long extra_attempts=10;

    // --------
    // Msg
    int msg=0;
    unsigned long long1=0;
    unsigned long long2=0;
    // --------
    unsigned long long3=0;
    // #todo: Get the button numberfor mouse clicks.

    int IsCombination=FALSE;

NextEvent:

    status = (int) rtl_get_event();

    if (status != TRUE)
    {
        for (i=0; i<extra_attempts; i++)
        {
            status = (int) rtl_get_event();
            if (status == TRUE)
                goto ProcessEvent;
        };
        goto fail;
    }

ProcessEvent:

    msg   = (int) (RTLEventBuffer[1] & 0xFFFFFFFF);
    long1 = (unsigned long) RTLEventBuffer[2];
    long2 = (unsigned long) RTLEventBuffer[3];
// #test
    long3 = (unsigned long) RTLEventBuffer[4]; //jiffie


// -----------------------
// MOUSE events
    if ( msg == GWS_MouseMove || 
         msg == GWS_MousePressed ||
         msg == GWS_MouseReleased )
    {
        // #test: For double click.
        if (msg == GWS_MousePressed){
        its_double_click = FALSE;
        if ( (long3-last_mousebutton_down_jiffie) < 250 ){
            //printf("DOUBLE\n");
            its_double_click = TRUE;
            msg = MSG_MOUSE_DOUBLECLICKED;
        }
        last_mousebutton_down_jiffie = long3; //jiffie
        }

        // 
        wmProcessMouseEvent(
            (int) msg,
            (unsigned long) long1,
            (unsigned long) long2 ); 
        
        // Processamos um evento de movimento,
        // provavelmente teremos outro subsequente.
        if (msg == GWS_MouseMove)
            goto NextEvent;
            
        return 0;
    }

// -----------------------
// Some keyboard events.
// Print char into the keyboard owner window.
    if ( msg == GWS_KeyDown ||
         msg == GWS_SysKeyDown ||
         msg == GWS_SysKeyUp )
    {
        wmProcessKeyboardEvent( 
            (int) msg, (unsigned long) long1, (unsigned long) long2 );
        return 0;
    }

// ---------------------------------
// Master timer
    if (msg == GWS_Timer)
    {
        // OK, it's working
        if (long1 == 1234)
        {
            //printf("Tick %d\n",long2);
            wmProcessTimerEvent(long1,long2);
        }
        return 0;
    }

// ---------------------------------
// Combination
// Is it a combination?
// The keyboard driver process the combination
// and send us the combination index.
    IsCombination = (int) is_combination(msg);
    int ComStatus = -1;
    if (IsCombination)
    {
        ComStatus = (int) wmProcessCombinationEvent(msg);
        // #todo
        // Can we return right here?
        // #test
        if (ComStatus == 0){
            return 0;
        }
        goto fail;
    }

// ??
// Hotkeys
    if (msg == GWS_HotKey)
    {
        // #todo: Call a worker for that.

        // Hot key id.
        // Activate the window associated with the given ID.
        if (long1 == 1){
            printf ("GWS_Hotkey 1\n");
        }
        if (long1 == 2){
            printf ("GWS_Hotkey 2\n");
        }
        // ...
    }

// Sys commands
    //if (msg == GWS_Command){
        // #todo: Call a worker for that.
    //}

// #test
// Notificando o display server que a resolução mudou.
// #todo
// Muidas estruturas aindapossuem valores que estão condizentes
// com a resolução antiga e precisa ser atualizados.

    if (msg == 800300)
    {
        printf("[800300] w=%d h=%d\n", long1, long2);
        
        /*
        //globals
        __device_width = long1;
        __device_height = long2;
        // globals
        SavedX=long1;
        SavedY=long2;
        // Update taskbar
        if ( (void*) taskbar_window != NULL )
        {
            taskbar_window->left = 0;
            taskbar_window->top = long2 - 32;
        }
        // Update working area.
        WindowManager.wa.height = (long2 - 40);
        */
        return 0;
    }

    //if (msg == GWS_Close)
    //    gwssrv_quit();

    //if (msg == GWS_UpdateDesktop)
    //    wm_update_desktop(TRUE,TRUE);

//Unknown:
    return 0;
fail:
    return (int) (-1);
}

// ------------------------------------------------
// wmInputReader2
// This is basically the low level input support 
// for the Gramado OS when running the Gramado Window System.
// This routine do not pump the messages from a file, just
// like the traditional way. It just get messages in a queue
// in the control thread of the display server process.
// The kernel post all the input messages into this queue for us.
// See: dev/tty in the kernel source code.
// ------------------------------------------------
// Read input from thread's queue.
// Esse nao eh o melhor metodo.
// #todo: precisamos ler de um arquivo que contenha
// um array de estruturas de eventos.
// #todo: Essas rotinas de input de dispositivo
// precisam ficar em bibliotecas. Mas de uma
// biblioteca pode existir no servidor, uma
// pra cada tipo de sistema.
// vamos tentar 32 vezes,
// pois nossa lista tem 32 ou 64 slots.
// Se encontrarmos um evento, entao pegamos ele.
// #bugbug: Isso eh um problema,
// pois quando nao tiver mensagens na fila,
// teremos que executar esse loop.
// #todo
// A mensagem de tecla pressionada
// deve vir com a informação de quanto
// tempo ela permaneceu pressionada.
// processamos ate 32 input válidos.
// isso deve ajudar quando movimentarmos o mouse.
// #importante:
// Se o servidor for portado para outro sistema
// então precisamos converter o formato do eventro
// entregue pelo sistema operacional, em um formato
// que o servidor consegue entender.
// Por enquanto o servidor entende o formato de eventos
// entregues pelo gramado.
// Called by main.c

int wmInputReader2(void)
{
// Getting input events from the event queue
// inside the control thread structure.
// Process all the messages in the queue, 
// starting at the first message.
// Disrespecting the circular input.

    int __Status = -1;
    register int i=0;
// see: event.h
    struct gws_event_d e;

    int IsCombination=FALSE;

// 32 slots
    for (i=0; i<MSG_QUEUE_MAX; i++)
    {
        // Não volte ao inicio da fila
        if(i< (MSG_QUEUE_MAX-1)) { __Status = rtl_get_event2(i,FALSE); }
        // Volte ao inicio da fila.
        if(i==(MSG_QUEUE_MAX-1)){ __Status = rtl_get_event2(i,TRUE);  }

        // #todo
        // Se a mensagem for um input de teclado,
        // então enviamos a mensagem 
        // para a janela com o foco de entrada.
        // Mensagens de outro tipo 
        // podem ir para outras janelas.
        
        if (__Status==TRUE)
        {
            // #test
            e.msg   = (int)           RTLEventBuffer[1];
            e.long1 = (unsigned long) RTLEventBuffer[2];
            e.long2 = (unsigned long) RTLEventBuffer[3];

            // MOUSE events
            // Calling procedure.
            if ( e.msg == GWS_MouseMove || 
                 e.msg == GWS_MousePressed ||
                 e.msg == GWS_MouseReleased )
            {
                wmProcessMouseEvent(
                    (int) e.msg,
                    (unsigned long) e.long1,
                    (unsigned long) e.long2 ); 
            }

            // keyboard
            // mensagens desse tipo
            // devem ir para a janela com o foco de entrada.
            if ( e.msg == GWS_KeyDown ||
                 e.msg == GWS_SysKeyDown ||
                 e.msg == GWS_SysKeyUp )
            {
                // Print char into the keyboard owner window.
                wmProcessKeyboardEvent( 
                    (int) e.msg, 
                    (unsigned long) e.long1, 
                    (unsigned long) e.long2 );
            }

            // Is it a combination?
            IsCombination = (int) is_combination(e.msg);
            if (IsCombination){
                wmProcessCombinationEvent(e.msg);
            }

            if (e.msg == GWS_HotKey)
            {
                // #todo: Call a worker for that.
                
                // Hot key id.
                // Activate the window associated with the given ID.
                if (e.long1 == 1){
                    printf ("GWS_Hotkey 1\n");
                }
                if (e.long1 == 2){
                    printf ("GWS_Hotkey 2\n");
                }
                // ...
            }

            //if (e.msg == GWS_Command){
                // #todo: Call a worker for that.
            //}
        }
    };

    return 0;
}

int xxGetAndProcessSystemEvents(void)
{
    return (int) wmInputReader();
}

void wm_change_bg_color(unsigned int color, int tile, int fullscreen)
{
// Change the custon background color.
    __set_custom_background_color(color);

    if ( (void*) __root_window == NULL ){
        return;
    }
    if (__root_window->magic!=1234){
        return;
    }
// Change
    __root_window->bg_color = (unsigned int) color;

// Validate
    if (fullscreen){
        wm_exit_fullscreen_mode(tile);
        return;
    }

// Tile
    if (tile){
        wm_update_desktop(TRUE,TRUE);
    }else{
        wm_update_desktop(FALSE,TRUE);
    };
}

// Enter fullscreen.
void wm_enter_fullscreen_mode(void)
{
    struct gws_window_d *w;

    if (WindowManager.initialized != TRUE){
        return;
    }

// active window
    //w = (struct gws_window_d *) last_window;
    w = (struct gws_window_d *) get_active_window();
    if ( (void*) w != NULL )
    {
        WindowManager.is_fullscreen = TRUE;
        WindowManager.fullscreen_window = 
            (struct gws_window_d *) w;
        wm_update_window_by_id(w->id);
        return;
    }

/*
// Last window
    w = (struct gws_window_d *) last_window;
    if ( (void*) w != NULL ){
        WindowManager.is_fullscreen = TRUE;
        WindowManager.fullscreen_window = 
            (struct gws_window_d *) w;
        wm_update_window_by_id(w->id);
        return;
    }
*/

/*
// First window
    w = (struct gws_window_d *) first_window;
    if ( (void*) w != NULL ){
        WindowManager.is_fullscreen = TRUE;
        WindowManager.fullscreen_window = 
            (struct gws_window_d *) w;
        wm_update_window_by_id(w->id);
        return;
    }
*/
}

void wm_exit_fullscreen_mode(int tile)
{
    if (WindowManager.initialized != TRUE){
        return;
    }
    WindowManager.is_fullscreen = FALSE;
    wm_update_desktop(tile,TRUE);
}

// yellow bar. (rectangle not window)
// developer status.
void yellowstatus0(char *string,int refresh)
{
// methods. get with the w.s., not with the system.
    unsigned long w = gws_get_device_width();
    unsigned long h = gws_get_device_height();
    unsigned long offset_string1 = 8;  //( 8*1 );
    //unsigned long offset_string2 = ( 8*5 );
    unsigned long bar_size = w;
    struct gws_window_d *aw;

// Validation
    //aw = (struct gws_window_d *) windowList[active_window];
    aw = (void*) active_window;
    if ( (void*) aw == NULL ){
        return;
    }
    if (aw->magic!=1234){
        return;
    }

    //if(aw->type!=WT_OVERLAPPED){
    //    return;
    //}

    //debug_print ("yellow_status:\n");
    
    //#todo
    //if ( (void*) string == NULL ){ return; }
    //if ( *string == 0 ){ return; }


// Desenha a barra no backbuffer

    if ( current_mode == GRAMADO_JAIL ){
        //bar_size = w;
        bar_size = (w>>1);
        doFillWindow ( 
            aw->absolute_x +2, 
            aw->absolute_y  +2, 
            bar_size, 
            24, 
            COLOR_YELLOW, 
            0 );
    }else{

        //bar_size = (offset_string2 + (4*8) );
        //bar_size = (offset_string2 + (100) );
        bar_size = (w>>1);
        doFillWindow ( 
            aw->absolute_x +2, 
            aw->absolute_y +2, 
            bar_size, 
            24, 
            COLOR_YELLOW, 
            0 );
    };

// Escreve as strings
    grDrawString ( 
        aw->absolute_x +2 + offset_string1, 
        aw->absolute_y +2 + 8, 
        COLOR_BLACK, 
        string );
    
    //grDrawString ( offset_string2, 8, COLOR_BLACK, "FPS" );
    
    // Mostra o retângulo.
     
    if (bar_size == 0){
        bar_size = 32;
    }

    if(refresh){
        gws_refresh_rectangle(
            (aw->absolute_x +2), (aw->absolute_y +2), bar_size, 24 );
    }
}

void yellow_status(char *string)
{
    if ( (void*)string==NULL ){
        return;
    }
    yellowstatus0(string,TRUE);
}

int 
is_within2 ( 
    struct gws_window_d *window, 
    unsigned long x, 
    unsigned long y )
{
    struct gws_window_d *pw;
    struct gws_window_d *w;

// #bugbug
// E se a janela tem janela mae?

// window validation
    if ( (void*) window == NULL ){
        return FALSE;
    }
    if ( window->used != TRUE && window->magic != 1234 ){
        return FALSE;
    }

// ====

// pw
// The parent window.
    pw = window->parent;
    if ( (void*) pw == NULL ){
        return FALSE;
    }
    if ( pw->used != TRUE && pw->magic != 1234 ){
        return FALSE;
    }

// w
// The window itself
    w = window;
    if ( (void*) w == NULL ){
        return FALSE;
    }
    if ( w->used != TRUE && w->magic != 1234 ){
        return FALSE;
    }

//relative to the parent.
    int x1= pw->absolute_x + w->absolute_x; 
    int x2= x1 + w->width;
    int y1= pw->absolute_y  + w->absolute_y;
    int y2= y1 + w->height;

    if( x > x1 && 
        x < x2 &&
        y > y1 && 
        y < y2 )
    {
        return TRUE;
    }

    return FALSE;
}


//#todo: Explain it.
int 
is_within ( 
    struct gws_window_d *window, 
    unsigned long x, 
    unsigned long y )
{
// #bugbug
// E se a janela tem janela mae?
    if ( (void*) window != NULL )
    {
        if ( window->used == TRUE && window->magic == 1234 )
        {
            if ( x >= window->absolute_x   && 
                 x <= window->absolute_right  &&
                 y >= window->absolute_y    &&
                 y <= window->absolute_bottom )
            {
                return TRUE;
            }
        }
    }
    return FALSE;
}

/*
void destroy_window (struct gws_window_d *window);
void destroy_window (struct gws_window_d *window)
{
    // #todo
    // if( window == __root_window)
        // return;
    if ( (void*) window != NULL )
    {
        if ( window->used == TRUE && window->magic == 1234 )
        {
            // ...
        }
    }
}
*/


// Color scheme
int gwssrv_initialize_default_color_scheme(void)
{
// Initialize the default color scheme.
// #todo: Put this routine in another document.

    struct gws_color_scheme_d *cs;

// HUMILITY
// Criando o esquema de cores humility. (cinza)

    cs = (void *) malloc( sizeof(struct gws_color_scheme_d) );
    if ((void *) cs == NULL)
    {
        gwssrv_debug_print ("gwssrv_initialize_color_schemes: cs\n");
        printf             ("gwssrv_initialize_color_schemes: cs\n"); 
        goto fail;
    }

    cs->initialized=FALSE;
    cs->id = 0;
    cs->name = "Humility";
    cs->style = 0;


// Colors
// size: 32 elements.
// see: 
// ws.h themes/honey.h


// 0
    cs->elements[csiNull] = 0;

// 1 - Screen background. (Wallpaper)
    cs->elements[csiDesktop] = 
        HONEY_COLOR_BACKGROUND;  

//----

// 2 - Window
    cs->elements[csiWindow] = 
        HONEY_COLOR_WINDOW;

// 3 - Window background
    cs->elements[csiWindowBackground] = 
        HONEY_COLOR_WINDOW_BACKGROUND;

// 4 - Border for active window.
    cs->elements[csiActiveWindowBorder] = 
        HONEY_COLOR_ACTIVE_WINDOW_BORDER;

// 5 - Border for inactive window.
    cs->elements[csiInactiveWindowBorder] = 
        HONEY_COLOR_INACTIVE_WINDOW_BORDER;

//----

// 6 - Titlebar for active window.
    cs->elements[csiActiveWindowTitleBar] = 
        HONEY_COLOR_ACTIVE_WINDOW_TITLEBAR;

// 7 - Titlebar for inactive window.
    cs->elements[csiInactiveWindowTitleBar] = 
        HONEY_COLOR_INACTIVE_WINDOW_TITLEBAR;

// 8 - Menubar
    cs->elements[csiMenuBar] = 
        HONEY_COLOR_MENUBAR;

// 9 - Scrollbar 
    cs->elements[csiScrollBar] = 
        HONEY_COLOR_SCROLLBAR;

// 10 - Statusbar
    cs->elements[csiStatusBar] = 
        HONEY_COLOR_STATUSBAR;

// 11 - Taskbar
    cs->elements[csiTaskBar] = 
        HONEY_COLOR_TASKBAR;

//----

// 12 - Messagebox
    cs->elements[csiMessageBox] = 
        HONEY_COLOR_MESSAGEBOX;

// 13 - System font. (Not a good name!)
    cs->elements[csiSystemFontColor] = 
        HONEY_COLOR_SYSTEMFONT;

// 14 - Terminal font.
    cs->elements[csiTerminalFontColor] = 
        HONEY_COLOR_TERMINALFONT;

// 15 - Button.
    cs->elements[csiButton] = 
        HONEY_COLOR_BUTTON;

// 16 - Window border.
    cs->elements[csiWindowBorder] = 
        HONEY_COLOR_WINDOW_BORDER;

// 17 - wwf border
    cs->elements[csiWWFBorder] = 
        HONEY_COLOR_WWF_BORDER;

// 18 - Titlebar text color.
    cs->elements[csiTitleBarTextColor] = 
        HONEY_COLOR_TITLEBAR_TEXT;

//
// Mouse hover
//

// 19 - When mousehover. (default color)
    cs->elements[csiWhenMouseHover] = 
        HONEY_COLOR_BG_ONMOUSEHOVER;
// 20 -
    cs->elements[csiWhenMouseHoverMinimizeControl] = 
        HONEY_COLOR_BG_ONMOUSEHOVER_MIN_CONTROL;
// 21 -
    cs->elements[csiWhenMouseHoverMaximizeControl] = 
        HONEY_COLOR_BG_ONMOUSEHOVER_MAX_CONTROL;
// 22 -
    cs->elements[csiWhenMouseHoverCloseControl] = 
        HONEY_COLOR_BG_ONMOUSEHOVER_CLO_CONTROL;

// 23 - Textbar text color
    cs->elements[csiTaskBarTextColor] = 
        xCOLOR_GRAY2;

    // ...

    cs->used  = TRUE;
    cs->magic = 1234;
    cs->initialized=TRUE;

// Salvando na estrutura padrão para o esquema humility.
    GWSCurrentColorScheme = (void*) cs;

// done:

    // OK
    return 0;

fail:
    printf("Couldn't initialize the default color scheme!\n");
    return -1;
}


unsigned int get_color(int index)
{
    unsigned int Result=0;

// Limits
    if (index<0 || index >= 32){
        goto fail;
    }
    if ( (void*) GWSCurrentColorScheme == NULL ){
        goto fail;
    }
    if (GWSCurrentColorScheme->magic!=1234){
        goto fail;
    }
    if (GWSCurrentColorScheme->initialized!=TRUE){
        goto fail;
    }

    Result = (unsigned int) GWSCurrentColorScheme->elements[index];

    return (unsigned int) Result;

fail:
// Invalid color?
    return (unsigned int) 0;
}

struct gws_window_d *get_window_from_wid(int wid)
{
    struct gws_window_d *w;

// wid
    if (wid<0 || wid>=WINDOW_COUNT_MAX)
        return NULL;
// Struture validation
    w = (void*) windowList[wid];
    if ( (void*) w == NULL )
        return NULL;
    if (w->magic!=1234)
        return NULL;

// Return the pointer
    return (struct gws_window_d *) w;
}

/*
// #todo
// Retorna o ponteiro de estrutura de janela
// dado o id da janela.
struct gws_window_d *gws_window_from_id (int id);
struct gws_window_d *gws_window_from_id (int id)
{
    struct gws_window_d *w;
    // ...
    return (struct gws_window_d *) w;
}
*/


void __create_start_menu(void)
{
// Colors for the taskbar and for the buttons.
    //unsigned int bg_color     = (unsigned int) get_color(csiTaskBar);
    unsigned int frame_color  = (unsigned int) get_color(csiTaskBar);
    unsigned int client_color = (unsigned int) get_color(csiTaskBar);

// ========================================
// Quick launch area buttons

    if ( (void*) taskbar_window == NULL ){
        printf("__create_start_menu: taskbar_window\n");
        exit(0);
    }
    if (taskbar_window->magic != 1234){
        printf("__create_start_menu: taskbar_window validation\n");
        exit(0);
    }



// ========================================
// Start menu button

    unsigned long sm_left= TB_BUTTON_PADDING;
    unsigned long sm_top = TB_BUTTON_PADDING;
    unsigned long sm_width = 
      ( QUICK_LAUNCH_AREA_PADDING -
        TB_BUTTON_PADDING -
        TB_BUTTON_PADDING );
    unsigned long sm_height = (taskbar_window->height -
        TB_BUTTON_PADDING -
        TB_BUTTON_PADDING );
    struct gws_window_d *sm_window;

    sm_window = 
        (struct gws_window_d *) CreateWindow ( 
            WT_BUTTON, 0, 1, 1, 
            "Gramado",  //string  
            sm_left, sm_top, sm_width, sm_height,   
            taskbar_window, 
            0, 
            frame_color,     // frame color 
            client_color );  // client window color

    if ( (void *) sm_window == NULL ){
        gwssrv_debug_print ("__create_start_menu: sm_window\n"); 
        printf             ("__create_start_menu: sm_window\n");
        exit(1);
    }
    if ( sm_window->used != TRUE || sm_window->magic != 1234 ){
        gwssrv_debug_print ("__create_start_menu: sm_window validation\n"); 
        printf             ("__create_start_menu: sm_window validation\n");
        exit(1);
    }

// Register the button.
    StartMenu.wid = RegisterWindow(sm_window);
    if (StartMenu.wid<0){
        gwssrv_debug_print ("__create_start_menu: Couldn't register sm_window\n");
        printf             ("__create_start_menu: Couldn't register sm_window\n");
        exit(1);
    }
    StartMenu.initialized = TRUE;
}

void __create_quick_launch_area(void)
{

// Colors for the taskbar and for the buttons.
    //unsigned int bg_color     = (unsigned int) get_color(csiTaskBar);
    unsigned int frame_color  = (unsigned int) get_color(csiTaskBar);
    unsigned int client_color = (unsigned int) get_color(csiTaskBar);


// ========================================
// Quick launch area buttons

    if ( (void*) taskbar_window == NULL ){
        printf("__create_quick_launch_area: taskbar_window\n");
        exit(0);
    }
    if (taskbar_window->magic != 1234){
        printf("__create_quick_launch_area: taskbar_window validation\n");
        exit(0);
    }

// ===================================
// button box:
// ===================================
// Clean the button list and the pid list.
    register int b=0;
    for (b=0; b<QL_BUTTON_MAX; b++)
    {
        QuickLaunch.buttons[b]=0;
        // ...
    };
    QuickLaunch.buttons_count=0;

    //unsigned long Space = TB_BUTTON_PADDING;   //4;
    //unsigned long b_width  = TB_BUTTON_WIDTH;    //(8*10);
    //unsigned long b_height = TB_BUTTON_HEIGHT;   //40-(Space*2);
    //unsigned long b_left   = TB_BUTTON_PADDING;  //Space;
    //unsigned long b_top    = TB_BUTTON_PADDING;  //Space;

    unsigned long b_left   = 0; 
    unsigned long b_top    = TB_BUTTON_PADDING;

    //unsigned long b_width  = TB_BUTTON_WIDTH;
    //unsigned long b_height = TB_BUTTON_HEIGHT;
    //unsigned long b_width  = (unsigned long)(tb_height -8);
    //unsigned long b_height = (unsigned long)(tb_height -8);
    unsigned long b_width  = (unsigned long)(taskbar_window->height -
        TB_BUTTON_PADDING -
        TB_BUTTON_PADDING );
    unsigned long b_height = (unsigned long)(taskbar_window->height -
        TB_BUTTON_PADDING -
        TB_BUTTON_PADDING );

    register int i=0;         //iterator
    int nbuttons=4;  //quantidade de botões na lista    
    struct gws_window_d *tmp_button;
    int tmp_wid=-1;
    char button_label[32];

// -----------------------------
// Quick launch area.
// Creating n buttons in the taskbar.
// #todo: We can make this options configurable.

    for (i=0; i<nbuttons; i++){

    b_left = 
        QUICK_LAUNCH_AREA_PADDING +
        TB_BUTTON_PADDING + ( (TB_BUTTON_PADDING*i) + (b_width*i) );

    itoa(i,button_label);
    button_label[2] = 0;
    
    tmp_button = 
        (struct gws_window_d *) CreateWindow ( 
            WT_BUTTON, 0, 1, 1, 
            button_label,  //string  
            b_left, b_top, b_width, b_height,   
            taskbar_window, 0, 
            frame_color,     // frame color 
            client_color );  // client window color

    if ( (void *) tmp_button == NULL ){
        gwssrv_debug_print ("__create_quick_launch_area: tmp_button\n"); 
        printf             ("__create_quick_launch_area: tmp_button\n");
        exit(1);
    }
    if ( tmp_button->used != TRUE || tmp_button->magic != 1234 ){
        gwssrv_debug_print ("__create_quick_launch_area: tmp_button validation\n"); 
        printf             ("__create_quick_launch_area: tmp_button validation\n");
        exit(1);
    }

// Register the button.
    tmp_wid = RegisterWindow(tmp_button);
    if (tmp_wid<0){
        gwssrv_debug_print ("__create_quick_launch_area: Couldn't register button\n");
        printf             ("__create_quick_launch_area: Couldn't register button\n");
        exit(1);
    }

    if (i==0){
        taskbar_startmenu_button_window = tmp_button;
    }

//save
    // id de janelas.
    QuickLaunch.buttons[i] = (int) tmp_wid;
    // ponteiros de estruturas de janelas do tipo botão.
    //tb_windows[i] = (unsigned long) tmp_button;

    // Número de botões criados.
    QuickLaunch.buttons_count++;
    };

    QuickLaunch.initialized = TRUE;
}

// Taskbar
// Display server's widget.
// Cria a barra na parte de baixo da tela.
// com 4 tags.
// os aplicativos podem ser agrupados por tag.
// quando uma tag eh acionada, o wm exibe 
// todos os aplicativos que usam essa tag.
void create_taskbar(int issuper, int show)
{
// Called by initGUI() in main.c

    unsigned long w = gws_get_device_width();
    unsigned long h = gws_get_device_height();
    int wid = -1;
// Colors for the taskbar and for the buttons.
    unsigned int bg_color     = (unsigned int) get_color(csiTaskBar);
    //unsigned int frame_color  = (unsigned int) get_color(csiTaskBar);
    //unsigned int client_color = (unsigned int) get_color(csiTaskBar);

    unsigned long tb_height = METRICS_TASKBAR_DEFAULT_HEIGHT;

    if (w==0 || h==0){
        gwssrv_debug_print ("create_taskbar: w h\n");
        printf             ("create_taskbar: w h\n");
        exit(1);
    }

    TaskBar.initialized = FALSE;
    //TaskBar.is_super = FALSE;
    //TaskBar.is_super = TRUE;
    TaskBar.is_super = (int) issuper;
    if (TaskBar.is_super != TRUE && TaskBar.is_super != FALSE)
    {
        TaskBar.is_super = FALSE;
    }

// Taskbar.
// Create  window.

    tb_height = METRICS_TASKBAR_DEFAULT_HEIGHT;
    if (TaskBar.is_super == TRUE){
        tb_height = METRICS_SUPERTITLEBAR_DEFAULT_HEIGHT;
    }

    //if (tb_height<40)
    if (tb_height<24){
        tb_height = 24;
    }
    if(tb_height >= h){
        tb_height = h-40;
    }

    unsigned long wLeft   = (unsigned long) 0;
    unsigned long wTop    = (unsigned long) (h-tb_height);
    unsigned long wWidth  = (unsigned long) w;
    unsigned long wHeight = (unsigned long) tb_height;  //40;

    TaskBar.left   = (unsigned long) wLeft;
    TaskBar.top    = (unsigned long) wTop;
    TaskBar.width  = (unsigned long) wWidth;
    TaskBar.height = (unsigned long) wHeight;

    taskbar_window = 
        (struct gws_window_d *) CreateWindow ( 
                                    WT_SIMPLE, 
                                    0, //style
                                    1, //status 
                                    1, //view
                                    "TaskBar",  
                                    wLeft, wTop, wWidth, wHeight,   
                                    gui->screen_window, 0, 
                                    bg_color, 
                                    bg_color );



// Struture validation
    if ( (void *) taskbar_window == NULL ){
        gwssrv_debug_print ("create_taskbar: taskbar_window\n"); 
        printf             ("create_taskbar: taskbar_window\n");
        exit(1);
    }
    if ( taskbar_window->used != TRUE || taskbar_window->magic != 1234 ){
        gwssrv_debug_print ("create_background: taskbar_window validation\n"); 
        printf             ("create_background: taskbar_window validation\n");
        exit(1);
    }

// Register the window.
    wid = (int) RegisterWindow(taskbar_window);
    if (wid<0){
        gwssrv_debug_print ("create_taskbar: Couldn't register window\n");
        printf             ("create_taskbar: Couldn't register window\n");
        exit(1);
    }

// wid
    taskbar_window->id = wid;
// Setup Window manager.
    WindowManager.taskbar = (struct gws_window_d *) taskbar_window;
// Show
    //flush_window(taskbar_window);

    TaskBar.initialized = TRUE;

// #debug

    /*
    printf ("bar: %d %d %d %d\n",
        taskbar_window->left,
        taskbar_window->top,
        taskbar_window->width,
        taskbar_window->height );

    //refresh_screen();
    //while(1){}
    */

//
// Start menu.
//

// Start menu.
    __create_start_menu();

//
// Quick launch area.
//

// Quick launch area.
    __create_quick_launch_area();
// Show
    if (show)
        flush_window_by_id(wid);
}

// Create root window
// Called by gwsInit in gws.c.
// #todo: Talvez essa função possa receber mais argumentos.
struct gws_window_d *wmCreateRootWindow(unsigned int bg_color)
{
    struct gws_window_d *w;
    int status=-1;

// It's because we need a window for drawind a frame.
// WT_OVERLAPPED needs a window and WT_SIMPLE don't.
    unsigned long rootwindow_valid_type = WT_SIMPLE;
    unsigned long left = 0;
    unsigned long top = 0;
// #bugbug: Estamos confiando nesses valores.
// #bugbug: Estamos usado device info sem checar.
    unsigned long width  = (unsigned long) (__device_width  & 0xFFFF );
    unsigned long height = (unsigned long) (__device_height & 0xFFFF );

    if (__device_width == 0 || __device_height == 0){
        debug_print("wmCreateRootWindow: w h\n");
        printf     ("wmCreateRootWindow: w h\n");
        exit(1);
    }

// Default background color.
    __set_default_background_color(bg_color);
    __set_custom_background_color(bg_color);

    // #debug
    // debug_print("wmCreateRootWindow:\n");

// (root window)
// #bugbug: Estamos usado device info sem checar.

    w = 
        (struct gws_window_d *) CreateWindow ( 
                                    rootwindow_valid_type,  
                                    0, //style
                                    1, //status
                                    1, //view
                                    "RootWindow",  
                                    left, top, width, height,
                                    NULL, 0, bg_color, bg_color );

// Struture validation
    if ( (void*) w == NULL){
        debug_print("wmCreateRootWindow: w\n");
        printf     ("wmCreateRootWindow: w\n");
        exit(1);
    }
    w->used = TRUE;
    w->magic = 1234;
// Buffers
    w->dedicated_buf = NULL;
    w->back_buf = NULL;
    w->front_buf = NULL;
    w->depth_buf = NULL;
// Device contexts
    w->window_dc = NULL;
    w->client_dc = NULL;

// Default dc.
    if ( (void*) gr_dc != NULL )
    {
        if (gr_dc->initialized == TRUE)
        {
            w->window_dc = (struct dc_d *) gr_dc;
            w->client_dc = (struct dc_d *) gr_dc;
        
            if ( (void*) CurrentProjection != NULL )
            {
                 if (CurrentProjection->magic == 1234)
                 {
                     if ( CurrentProjection->initialized == TRUE ){
                         CurrentProjection->dc = (struct dc_d *) gr_dc;
                     } 
                 }
            }
        }
    }

    w->is_solid = TRUE;
    w->rop = 0;
// Setup the surface in ring0
    setup_surface_rectangle(left,top,width,height);
// invalidate the surface in ring0.
    invalidate_surface_retangle();
    w->dirty = TRUE;  // Invalidate again.
    w->locked = TRUE;

// Register root window.
    status = gwsDefineInitialRootWindow(w);
    if (status<0){
        printf("wmCreateRootWindow: Couldn't register root window\n");
        exit(0);
    }

// #
// Do not register now.
// The caller will do that thing.
    return (struct gws_window_d *) w;
}

// OUT: 
// 0=ok | <0=Fail.
int gwsDefineInitialRootWindow (struct gws_window_d *window)
{

// Structure validation
    if ( (void *) window == NULL )
        return -1;
    if (window->magic != 1234)
        return -1;

// Set
    __root_window      = (struct gws_window_d *) window;
    WindowManager.root = (struct gws_window_d *) window;
// OK.
    return 0;
}

int dock_active_window(int position)
{
// Dock the active window into a given corner.

    struct gws_window_d *w;
    
    //int wid=-1;
    //wid = (int) get_active_window();
    //if(wid<0)
    //    return -1;
    //if(wid>=WINDOW_COUNT_MAX)
    //    return -1;
    //w = (struct gws_window_d *) windowList[wid];

// Structure validation
// #todo: Use a worker. 
// Create one if we dont have it yet.
    w = (void*) active_window;
    if ( (void*) w == NULL ){
        return -1;
    }
    if (w->magic!=1234){
        return -1;
    }

// Can't be the root.
    if (w == __root_window){
        return -1;
    }
// Can't be the taskbar.
    if (w == taskbar_window){
        return -1;
    }
// Dock
    dock_window(w,position);
    return 0;
}

int dock_window( struct gws_window_d *window, int position )
{
// Dock a given window into a given corner.
// Not valid in fullscreen mode.

    if (WindowManager.initialized != TRUE){
        return -1;
    }
// Can't dock a window in fullscreen mode.
    if (WindowManager.is_fullscreen == TRUE){
        return -1;
    }

// todo
// Use the working area, not the device area.
    //unsigned long w = gws_get_device_width();
    //unsigned long h = gws_get_device_height();
    //#test
    //Using the working area
    unsigned long w = WindowManager.wa.width;
    unsigned long h = WindowManager.wa.height;
    if ( w==0 || h==0 ){
        return -1;
    }

// #bugbug
// Validate the max value.

// Structure validation
    if ( (void*) window == NULL ){
        return -1;
    }
    if (window->magic!=1234){
        return -1;
    }

// Can't be the root
    if (window == __root_window){
        return -1;
    }
// Can't be the active window
    if (window == taskbar_window){
        return -1;
    }

// Window type
    if (window->type == WT_BUTTON){
        return -1;
    }

    switch (position){

        // top
        // #todo: maximize in this case
        case 1:
            gwssrv_change_window_position( window, 0, 0 );
            gws_resize_window( window, w, h );
            //gws_resize_window( window, w, h>>1 );
            break;
        //right
        case 2:
            gwssrv_change_window_position( window, (w>>1), 0 );
            gws_resize_window( window, (w>>1)-4, h-4 );
            break;
        //bottom
        //#todo: restore or put it in the center.
        case 3:
            gwssrv_change_window_position( window, 0, h>>2 );
            //gwssrv_change_window_position( window, 0, h>>1 );
            gws_resize_window( window, w, h>>1 );
            break;
        //left
        case 4:
            gwssrv_change_window_position( window, 0, 0 ); 
            gws_resize_window( window, (w>>1)-4, h-4 );
            break;

        default:
            return -1;
            break;
    };


    set_active_window(window);
    //set_focus(window);
    redraw_window(window,TRUE);
    // Post message to the main window.
    // Paint the childs of the window with focus.
    on_update_window(window,GWS_Paint);

    return 0; 
}

struct gws_window_d *get_active_window (void)
{
    return (struct gws_window_d *) active_window;
}

void set_active_window(struct gws_window_d *window)
{
// #bugbug
// Can we active the root window?
// The root window is WT_SIMPLE.

    if (window == active_window){
        return;
    }

    if ((void*) window == NULL){
        return;
    }
    if (window->magic!=1234){
        return;
    }

    active_window = (void*) window;
    mouse_owner = (void*) window;
}

void unset_active_window(void)
{
    active_window = NULL;
}

// Pega o ponteiro da janela com foco de entrada.
struct gws_window_d *get_window_with_focus(void)
{
    struct gws_window_d *w;

    w = (struct gws_window_d *) keyboard_owner;
    if ((void*)w==NULL){
        return NULL;
    }
    if (w->used != TRUE){
        keyboard_owner = NULL;
        return NULL;
    }
    if (w->magic != 1234){
        keyboard_owner = NULL;
        return NULL; 
    }

    return (struct gws_window_d *) w; 
}

void set_window_with_focus(struct gws_window_d * window)
{
    if (window == keyboard_owner)
        return;

    if ( (void*) window == NULL )
        return;
    if (window->magic!=1234)
        return;

    keyboard_owner = (void*) window; 
/*  
//#test
    struct gws_window_d *w;
    w = (struct gws_window_d *) windowList[id];
    sc82 (10011,w->client_tid,w->client_tid,w->client_tid);
*/
}


// Pegando a z-order de uma janela.
int get_zorder ( struct gws_window_d *window )
{
    if ( (void *) window != NULL ){
        return (int) window->zIndex;
    }

    return (int) -1;
}


struct gws_window_d *get_top_window (void)
{
    return (struct gws_window_d *) top_window;
}

// Setando a top window.
void set_top_window (struct gws_window_d *window)
{
    if (window == top_window)
        return;

    if ( (void*) window == NULL )
        return;
    if (window->magic!=1234)
        return;

    top_window = (void*) window;
}


int 
gws_resize_window ( 
    struct gws_window_d *window, 
    unsigned long cx, 
    unsigned long cy )
{
    struct gws_window_d *tmp_window;
    int tmp_wid = -1;


    if ( (void *) window == NULL ){
        return -1;
    }

// #todo
    //if(window == __root_window)
        //return -1;

// Só precisa mudar se for diferente.
    if ( window->width != cx || window->height != cy )
    {

        // Temos um valor mínimo no caso
        // de janelas do tipo overlapped.
        // Mesma coisa para o valor máximo.
        // Uma janela overlapped não pode ser to tamanho da tela,
        // mesmo que estejamos em modo fullscreen, pois o modo
        // full screen usa apenas o conteúdo da área de cliente,
        // não a janela do tipo overlapped.
        if (window->type == WT_OVERLAPPED)
        {
            if (cx < METRICS_DEFAULT_MINIMUM_WINDOW_WIDTH){
                cx = METRICS_DEFAULT_MINIMUM_WINDOW_WIDTH;
            }
            if (cy < METRICS_DEFAULT_MINIMUM_WINDOW_HEIGHT){
                cy = METRICS_DEFAULT_MINIMUM_WINDOW_HEIGHT;
            }

            if (WindowManager.initialized==TRUE)
            {
                if (cx > WindowManager.wa.width){
                    cx=WindowManager.wa.width;
                }
                if (cy > WindowManager.wa.height){
                    cy=WindowManager.wa.height;
                }
            }
        }

        window->width = (unsigned long) cx;
        window->height = (unsigned long) cy;

        // Muda tambem as dimençoes da titlebar.
        // Muda somente a largura, pois a altura deve 
        // continuar a mesma;
        if (window->type == WT_OVERLAPPED)
        {
            // titlebar
            if ( (void*) window->titlebar != NULL )
            {
                window->titlebar->width = 
                    (window->width - window->border_size - window->border_size );

                // ============
                // minimize
                tmp_wid = 
                    (int) window->titlebar->Controls.minimize_wid;
                tmp_window = 
                    (struct gws_window_d *) get_window_from_wid(tmp_wid);
                if ( (void*) tmp_window != NULL )
                {
                    if (tmp_window->magic == 1234)
                    {
                        // width - left_offset.
                        tmp_window->left = 
                            (unsigned long) (window->titlebar->width - 
                             tmp_window->left_offset);
                    }
                }

                // ============
                // maximize
                tmp_wid = 
                    (int) window->titlebar->Controls.maximize_wid;
                tmp_window = 
                    (struct gws_window_d *) get_window_from_wid(tmp_wid);
                if ( (void*) tmp_window != NULL )
                {
                    if (tmp_window->magic == 1234)
                    {
                        // width - left_offset.
                        tmp_window->left = 
                            (unsigned long) (window->titlebar->width - 
                             tmp_window->left_offset);
                    }
                }

                // ============
                // close
                tmp_wid = 
                    (int) window->titlebar->Controls.close_wid;
                tmp_window = 
                    (struct gws_window_d *) get_window_from_wid(tmp_wid);
                if ( (void*) tmp_window != NULL )
                {
                    if (tmp_window->magic == 1234)
                    {
                        // width - left_offset.
                        tmp_window->left = 
                            (unsigned long) (window->titlebar->width - 
                             tmp_window->left_offset);
                    }
                }


            }

            // client area . (rectangle).

            // width
            // menos bordas laterais
            window->rcClient.width = 
                (unsigned long) (window->width -2 -2 );
            // height
            // menos bordas superior e inferior
            // menos a barra de tarefas.
            //#bugbug: e se a janela for menor que 32?
            window->rcClient.height = 
                (unsigned long) (window->height -2 -32 -2); 
        }
    }

// #bugbug
// Precisa mesmo pintar toda vez que mudar as dimensoes
    //invalidate_window(window);
    return 0;
}


// #test
// Isso so faz sentido num contexto de reinicializaçao 
// do desktop.
void reset_zorder(void)
{
     register int i=0;
     struct gws_window_d *w;

     for ( i=0; i<WINDOW_COUNT_MAX; ++i)
     {
         w = (struct gws_window_d *) windowList[i];
         if ( (void*) w != NULL )
         {
             if ( w->used == TRUE && w->magic == 1234 )
             {
                 // Coloca na zorder as janelas overlapped.
                 if (w->type == WT_OVERLAPPED){
                     zList[i] = windowList[i];
                 }
             }
         }
     };
}

// gwssrv_change_window_position:
// Muda os valores do posicionamento da janela.
// #todo: Podemos mudar o nome para wm_change_window_position().
int 
gwssrv_change_window_position ( 
    struct gws_window_d *window, 
    unsigned long x, 
    unsigned long y )
{
// Isso deve mudar apenas o deslocamento em relacao
// a margem e nao a margem?

// #test
// Quando uma janela overlapped muda de posição,
// as janelas que compoem o frame vão acompanhar esse deslocamento
// pois suas posições são relativas.
// Mas no caso das janelas filhas, criadas pelos aplicativos,
// precisarão atualizar suas posições. Ou deverão armazenar
// suas posições relativas à sua janela mãe.
// #test
// Nesse momento, podemos checar, quais janelas possuem essa janela
// como janela mãe, e ... ?

    struct gws_window_d *tmp_window;
    int tmp_wid = -1;

    if ((void *) window == NULL){
        gwssrv_debug_print("gwssrv_change_window_position: window\n");
        return -1;
    }

// #todo
    //if(window == __root_window)
        //return -1;

    /*
    if ( window->left != x ||
         window->top  != y )
    {
        window->left = (unsigned long) x;
        window->top  = (unsigned long) y;
    }
    */

// #bugbug #todo
// Temos que checar a validade da parent.

// absoluto

// -------------


    if (window->type == WT_OVERLAPPED)
    {
        window->absolute_x = (unsigned long) x;
        window->absolute_y = (unsigned long) y;
        window->absolute_right = 
            (unsigned long) (x + window->width);
        window->absolute_bottom = 
            (unsigned long) (y + window->height );
    }   

    if (window->type != WT_OVERLAPPED)
    {
        window->absolute_x = 
            (unsigned long) (window->parent->absolute_x + x);
        window->absolute_y = 
            (unsigned long) (window->parent->absolute_y + y);
        window->absolute_right = 
            (unsigned long) (window->absolute_x + window->width);
        window->absolute_bottom = 
            (unsigned long) (window->absolute_y + window->height );
    }

// -------------

// relativo
    window->left = x;
    window->top = y;

// Se overlapped:
// Muda também as posições da titlebar.
// Muda também as posições do área de cliente.
    if (window->type == WT_OVERLAPPED)
    {
        // Title bar window.
        if ( (void*) window->titlebar != NULL )
        {
            window->titlebar->absolute_x = 
                ( window->absolute_x + window->border_size );
            window->titlebar->absolute_y = 
                ( window->absolute_y  + window->border_size );
        
            
            if (window->titlebar->Controls.initialized == TRUE)
            {
                // get pointer from id.
                // change position of the controls.

                // #todo
                // We need a worker for that job.

                //===============
                // Change position for minimize.
                tmp_wid = 
                    (int) window->titlebar->Controls.minimize_wid;
                tmp_window = 
                    (struct gws_window_d *) get_window_from_wid(tmp_wid);
                if ( (void*) tmp_window != NULL )
                {
                    if (tmp_window->magic == 1234)
                    {
                        tmp_window->absolute_x = 
                            ( window->titlebar->absolute_x + tmp_window->left );
                        tmp_window->absolute_y = 
                            ( window->titlebar->absolute_y + tmp_window->top );
                    }
                }

                //===============
                // Change position for maximize.
                tmp_wid = 
                    (int) window->titlebar->Controls.maximize_wid;
                tmp_window = 
                    (struct gws_window_d *) get_window_from_wid(tmp_wid);
                if ( (void*) tmp_window != NULL )
                {
                    if (tmp_window->magic == 1234)
                    {
                        tmp_window->absolute_x = 
                            ( window->titlebar->absolute_x + tmp_window->left );
                        tmp_window->absolute_y = 
                            ( window->titlebar->absolute_y + tmp_window->top );
                    }
                }

                //===============
                // Change position for close.
                tmp_wid = 
                    (int) window->titlebar->Controls.close_wid;
                tmp_window = 
                    (struct gws_window_d *) get_window_from_wid(tmp_wid);
                if ( (void*) tmp_window != NULL )
                {
                    if (tmp_window->magic == 1234)
                    {
                        tmp_window->absolute_x = 
                            ( window->titlebar->absolute_x + tmp_window->left );
                        tmp_window->absolute_y = 
                            ( window->titlebar->absolute_y + tmp_window->top );
                    }
                }



            }
        }
        // Client area . (rectangle).
        // Esses valores são relativos, então não mudam.
    }


// Se nossa parent é overlapped.
// Entao estamos dentro da area de cliente.
    struct gws_window_d *p;
    p = window->parent;
    if ( (void*) p != NULL )
    {
        if (p->magic == 1234)
        {
            if (p->type == WT_OVERLAPPED)
            {
                window->absolute_x = 
                   p->absolute_x +
                   p->rcClient.left +
                   window->left;

                window->absolute_y = 
                   p->absolute_y +
                   p->rcClient.top +
                   window->top;
            }
        }
    }


// #bugbug
// Precisa mesmo pinta toda vez que mudar a posiçao?
    //invalidate_window(window);
    return 0;
}


// Lock a window. 
void gwsWindowLock (struct gws_window_d *window)
{
    if ( (void *) window == NULL ){
        return;
    }
    window->locked = (int) WINDOW_LOCKED;  //1.
}

// Unlock a window. 
void gwsWindowUnlock (struct gws_window_d *window)
{
    if ( (void *) window == NULL ){
        return;
    }
    window->locked = (int) WINDOW_UNLOCKED;  //0.
}


int gwssrv_init_windows(void)
{
    register int i=0;

    //window.h
    windows_count     = 0;
    keyboard_owner = NULL;
    active_window = NULL;
    top_window    = NULL;
    //...
    show_fps_window = FALSE;

// Window list
    for (i=0; i<WINDOW_COUNT_MAX; ++i){ windowList[i] = 0; };
// z order list
    for (i=0; i<ZORDER_MAX; ++i){ zList[i] = 0; };
    // ...

    return 0;
}

//
// == ajusting the window to fit in the screen. =======================
//

/*
 credits: hoppy os.
void 
set_window_hor (
    tss_struct *tss,
    int i,
    int j)
{
    int d = j-i;
    
    if ( i >= tss->crt_width) 
    {
        i = tss->crt_width-2;
        j = i+d;
    }
    
    if (j<0) 
    {
        j=0;
        i = j-d;
    }
    
    if (i>j) 
    {
        if (i>0)
           j=i;
        else
            i=j;
    }
    
    tss->window_left=i;
    tss->window_right=j;
}
*/


/*
 credits: hoppy os.
void 
set_window_vert ( 
    tss_struct *tss,
    int i,
    int j )
{
    int d = j-i;

    if (i >= tss->crt_height) 
    {
        // ajusta o i.
        i = tss->crt_height-1;
        j = i+d;
    }
    
    if (j<0) 
    {
        // ajusta o i.
        j = 0;
        i = j-d;
    }

    if (i>j) 
    {
        if (i>0)
            j=i;
        else
            i=j;
    }

    tss->window_top    = i;
    tss->window_bottom = j;
}
*/


/*
// process id
// Retorna o pid associado à essa janela.
// #todo: fazer essa rotina também na biblioteca client side.
pid_t get_window_pid( struct gws_window_d *window);
pid_t get_window_pid( struct gws_window_d *window)
{
}
*/

/*
// thread id
// Retorna o tid associado à essa janela.
// #todo: fazer essa rotina também na biblioteca client side.
int get_window_tid( struct gws_window_d *window);
int get_window_tid( struct gws_window_d *window)
{
}
*/

// teremos mais argumentos
void wm_Update_TaskBar(char *string, int flush)
{
    if ( WindowManager.is_fullscreen == TRUE ){
        return;
    }
    if ( (void*) string == NULL ){
        return;
    }
    if (*string == 0){
        return;
    }
// TaskBar
    if (TaskBar.initialized != TRUE)
        return;
// Window
    if ( (void*) taskbar_window == NULL ){
        return;
    }
    if (taskbar_window->magic!=1234){
        return;
    }

// Redraw the bar.
// Redraw the buttons.
    //redraw_window_by_id(taskbar_window->id,TRUE);
    redraw_window_by_id(taskbar_window->id,FALSE);

    //redraw_window_by_id(
    //    __taskbar_startmenu_button_window->id,TRUE);
    //redraw_window(taskbar_window,TRUE);
    //redraw_window(__taskbar_startmenu_button_window,TRUE);

    //redraw_window_by_id(StartMenu.wid,TRUE);
    redraw_window_by_id(StartMenu.wid,FALSE);
    
// Redraw, all the valid buttons in the list.
    register int i=0;
    int wid=0;
    for (i=0; i<QL_BUTTON_MAX; i++)
    {
        if (QuickLaunch.buttons[i] != 0)
        {
            wid = (int) QuickLaunch.buttons[i];
            redraw_window_by_id(wid,FALSE);
            //__draw_button_mark_by_wid(wid,i);
        }
    };

//
// Strings
//

// String info

    // fg color
    unsigned int string_color = 
        (unsigned int) get_color(csiTaskBarTextColor);

    // String 1 left (Text)
    unsigned long string1_left = 
        (unsigned long) (taskbar_window->width - 100);

    // String 2 left (Separator)
    unsigned long string2_left = 
        (unsigned long) (string1_left - (8*2));

    // String top
    unsigned long string_top = 
        ((taskbar_window->height-8) >> 1);  //8 

    // String size
    size_t string_size = (size_t) strlen(string);

/*
// #unused
// Frame counter support
    char frame_counter_string[32];
    itoa(
        WindowManager.frame_counter,
        frame_counter_string );
*/

/*
// #todo
//---------------------------------------------
// String 3
// 800x600
    if (current_mode == GRAMADO_HOME)
    {
        dtextDrawText(
            taskbar_window, 
            (taskbar_window->width >> 1), 
            string_top, 
            string_color, 
            "++Testing++" );
    }
*/

// ----------------------------
// String 2: The separator '|'.
    dtextDrawText(
        taskbar_window, 
        string2_left, 
        string_top, 
        string_color, 
        "|" );

// ----------------------------
// String 1: Draw the text.
// Less than 10 chars. (8*10)=80, the left is -100.
    if (string_size <= 10)
    {
        // String 1
        dtextDrawText(
            taskbar_window,
            string1_left, string_top, string_color, string );
    }

// Show the window
    if (flush == TRUE){
        flush_window(taskbar_window);
    }
}

void wmInitializeGlobals(void)
{
    //debug_print ("wmInitializeGlobals:\n");
    fps=0;
    frames_count=0;
    ____old_time=0;
    ____new_time=0;
    old_x=0;
    old_y=0;
    
    grab_is_active = FALSE;
    is_dragging = FALSE;
    grab_wid = -1;
}

//
// End
//


