/*
  fsm@robots.ox.ac.uk
*/
#include "vgui_glut.h"
#include "vgui_glut_window.h"
#include <vcl_cstdlib.h>
#include <vcl_cassert.h>
#include <vgui/vgui_glut.h>

//--------------------------------------------------------------------------------

int vgui_glut::count = 0;

vgui_glut::vgui_glut()
{
  ++count;
  assert(count == 1);
}

vgui_glut::~vgui_glut()
{
  --count;
  assert(count==0);
}

//--------------------------------------------------------------------------------

void vgui_glut::init(int &argc, char **argv)
{
//   cerr << __FILE__ " init() :" << endl;
//   for (unsigned i=0; i<argc; ++i)
//     cerr << i << ' ' << (void*)argv[i] << flush << ' ' << argv[i] << endl;
  glutInit(&argc, argv);
}

vcl_string vgui_glut::name() const
{
  return "glut";
}

vgui_window *vgui_glut::produce_window(int width, int height,
                                       vgui_menu const &menubar,
                                       char const *title)
{
  vgui_glut_window *win = new vgui_glut_window(title, width, height);
  win->menubar = menubar;
  return win;
}

vgui_window *vgui_glut::produce_window(int width, int height,
                                       char const *title)
{
  return new vgui_glut_window(title, width, height);
}

//----------------------------------------------------------------------

#include <vcl_cstring.h> // memcpy()
#include <vcl_csetjmp.h>
#include <vcl_iostream.h>
#include <vgui/vgui_macro.h>

// Use setjmp()/longjmp() to circumvent GLUT event loop restrictions.
//
// It is essential never to longjmp() across C++ destructors. It
// happens to be safe to longjmp() out of glutMainLoop() from the
// GLUT idle callback, by inspection of the GLUT sources.
//
// main()
//  ...
//   vgui::run()
//    ...
//    vgui_glut::run()
//    ...
//     internal_run_till_idle();
//     goto next_statement;// this is what setjmp() effectively does
//  longjmp_target: // When the idle callback is called, it will longjmp to here.
//     goto idle;
//  next_statement:
//     glutMainLoop()
//     vcl_exit(); // [gets here only on close of window.]
//
//  idle:
//       [user code]
//       ...

static int const   internal_label = 1234;
static vcl_jmp_buf internal_buf;

// This function is the idle callback used
// to longjmp() out of the GLUT event loop.
static
void internal_longjmp_idler()
{
  vcl_longjmp(internal_buf, internal_label);
  assert(false);
}

// This function lets the GLUT event loop run till it becomes
// idle and then returns to the caller. It is intended to be
// re-entrant, hence the saving and restoring of the jmp_buf.
static
void internal_run_till_idle()
{
  // save the current jmp_buf;
  vcl_jmp_buf saved_buf;
  vcl_memcpy(&saved_buf, &internal_buf, sizeof internal_buf);

  // record current state/accept control after longjmp().
  int t = setjmp(internal_buf);

/*longjmp_target:*/
  // if we got back control after a longjmp(), restore
  // the previous jmp_buf and return to the caller now.
  if (t != 0) {
    assert(t == internal_label);
    vcl_memcpy(&internal_buf, &saved_buf, sizeof internal_buf);
    return;
  }

/*next_statement:*/
  // set idle function.
  glutIdleFunc(internal_longjmp_idler);

  // call GLUT event loop. when the event queue becomes empty, the idle callback
  // will be called and that then longjmp()s back.
  glutMainLoop();

  // If we get here, it means glutMainLoop()
  // returned, which it should never do.
  vgui_macro_warning << "internal error in internal_run_till_idle_wrapped()" << vcl_endl;
  vgui_macro_warning << "please report to fsm@robots.ox.ac.uk" << vcl_endl;
  vcl_abort();
}

//--------------------------------------------------------------------------------

#include <vcl_list.h>
#include <vcl_utility.h>
#include <vgui/vgui_command.h>
#include "vgui_glut_adaptor.h"

static
vcl_list<vcl_pair<void *, void *> > vgui_glut_command_queue;

void vgui_glut_queue_command(vgui_glut_adaptor *a, vgui_command *c)
{
  c->ref(); // matched by unref() in process_command_queue();
  vgui_glut_command_queue.push_back(vcl_pair<void *, void *>(a, c));
}

static
void vgui_glut_process_command_queue()
{
  while (! vgui_glut_command_queue.empty()) {
    // remove from front of queue.
    vcl_pair<void *, void *> p = vgui_glut_command_queue.front();
    vgui_glut_command_queue.pop_front();
    
    // a bit of casting.
    vgui_glut_adaptor *a = static_cast<vgui_glut_adaptor *>(p.first );
    vgui_command      *c = static_cast<vgui_command *>(p.second);
    
    // switch to the relevant GL context.
    int old_win = glutGetWindow();
    if (old_win != a->get_id())
      glutSetWindow(a->get_id());
    
    // execute the command.
    //vcl_cerr << "cmnd = " << (void*)vgui_glut_adaptor_menu_command << vcl_endl;
    c->execute();
    //vcl_cerr << "returned successfully" << vcl_endl;
    
    // this matches ref() in vgui_glut_queue_command()
    c->unref();
    
    // switch back to the old GL context.
    if (old_win != 0 && old_win != a->get_id())
      glutSetWindow(old_win);
  }
}

// When set, this flag indicates that the event
// loop should be terminated in the near future.
static bool internal_quit_flag = false;

void vgui_glut::run()
{
  internal_quit_flag = false;
  while (! internal_quit_flag) {
    internal_run_till_idle();
    vgui_glut_process_command_queue();
  }
  vgui_macro_warning << "end of vgui_glut event loop" << vcl_endl;
}

// This is (erroneously) called from vgui_glut_adaptor::post_destroy().
void vgui_glut_quit()
{
  internal_quit_flag = true;
}

void vgui_glut::quit()
{
  internal_quit_flag = true;
}

// This is actually run-a-few-events, sorry...
void vgui_glut::run_one_event()
{
  internal_run_till_idle();
  vgui_glut_process_command_queue();
}

void vgui_glut::run_till_idle()
{
  internal_run_till_idle();
  vgui_glut_process_command_queue();
}

//--------------------------------------------------------------------------------
