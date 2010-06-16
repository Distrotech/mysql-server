#include "sql_string.h"

#ifdef HAVE_SPATIAL

#include "gcalc_slicescan.h"

#ifdef GCALC_DBUG
void gcalc_dbug_env_struct::start_line()
{
  if (recfile)
  {
    fprintf(recfile, "LINESTRING(");
    first_point= 1;
  }
}


void gcalc_dbug_env_struct::start_ring()
{
  if (recfile)
  {
    fprintf(recfile, "RING(");
    first_point= 1;
  }
}


void gcalc_dbug_env_struct::complete()
{
  if (recfile)
  {
    fprintf(recfile, ")\n");
    fflush(recfile);
  }
}


void gcalc_dbug_env_struct::add_point(double x, double y)
{
  if (recfile)
  {
    if (!first_point)
      fprintf(recfile, ", ");
    else
      first_point=0;
    fprintf(recfile, "%.15g %.15g", x, y);
  }
}


void gcalc_dbug_env_struct::start_newfile(const char *filename)
{
  recfile= fopen(filename, "w");
}


void gcalc_dbug_env_struct::start_append(const char *filename)
{
  recfile= fopen(filename, "a");
}


void gcalc_dbug_env_struct::stop_recording()
{
  fclose(recfile);
  recfile= NULL;
}


void gcalc_dbug_env_struct::print(const char *ln)
{
  if (recfile)
    fprintf(recfile, "%s", ln);
}


gcalc_dbug_env_struct::~gcalc_dbug_env_struct()
{
  if (recfile)
    fclose(recfile);
}


void gcalc_dbug_do_print(const char* fmt, ...)
{
  va_list args;
  char buff[1000];
  va_start(args, fmt);
  vsnprintf(buff, sizeof(buff), fmt, args);
  va_end(args);
  gcalc_dbug_cur_env->print(buff);
}


void gcalc_scan_iterator::point::dbug_print()
{
  const gcalc_scan_iterator::point *slice= this;
  for (; slice; slice= slice->get_next())
  {
    gcalc_dbug_do_print("(%d %.15g ", slice->thread, slice->x);
    gcalc_dbug_do_print(slice->horiz_dir ? "-" : "/");
    gcalc_dbug_do_print(" %.15g) ", slice->dx_dy);
  }
  gcalc_dbug_do_print("\n");
}


static gcalc_dbug_env_struct gcalc_dbug_usual_env;
gcalc_dbug_env_struct *gcalc_dbug_cur_env= &gcalc_dbug_usual_env;

#endif /*GCLAC_DBUG*/

#define PH_DATA_OFFSET 8
#define coord_to_float(d) ((double) d)

typedef int (*sc_compare_func)(const void*, const void*);

#define LS_LIST_ITEM gcalc_dyn_list::item
#define LS_COMPARE_FUNC_DECL sc_compare_func compare,
#define LS_COMPARE_FUNC_CALL(list_el1, list_el2) (*compare)(list_el1, list_el2)
#define LS_NEXT(A) (A)->next
#define LS_SET_NEXT(A,val) (A)->next= val
#define LS_P_NEXT(A) &(A)->next
#define LS_NAME sort_list
#define LS_SCOPE static
#define LS_STRUCT_NAME sort_list_stack_struct
#include "plistsort.c"


gcalc_dyn_list::gcalc_dyn_list(size_t blk_size, size_t sizeof_item):
  m_blk_size(blk_size - ALLOC_ROOT_MIN_BLOCK_SIZE),
  m_sizeof_item(ALIGN_SIZE(sizeof_item)),
  m_points_per_blk((m_blk_size - PH_DATA_OFFSET) / m_sizeof_item),
  m_blk_hook(&m_first_blk),
  m_free(NULL),
  m_keep(NULL)
{}


gcalc_dyn_list::item *gcalc_dyn_list::alloc_new_blk()
{
  void *new_block= my_malloc(m_blk_size, MYF(MY_WME));
  item *result, *pi_end, *cur_pi;

  if (!new_block)
    return NULL;
  *m_blk_hook= new_block;
  m_blk_hook= (void**)new_block;
  result= (item *)(((char *)new_block) + PH_DATA_OFFSET);
  pi_end= ptr_add(result, m_points_per_blk - 1);
  cur_pi= ptr_add(result, 1);
  m_free= cur_pi;
  while (cur_pi<pi_end)
    cur_pi= cur_pi->next= ptr_add(cur_pi, 1);
  cur_pi->next= NULL;
  return result;
}


static void free_blk_list(void *list)
{
  void *next_blk;
  while (list)
  {
    next_blk= *((void **)list);
    my_free(list, MYF(0));
    list= next_blk;
  }
}


void gcalc_dyn_list::cleanup()
{
  *m_blk_hook= NULL;
  free_blk_list(m_first_blk);
  m_first_blk= NULL;
  m_blk_hook= &m_first_blk;
  m_free= NULL;
}


gcalc_dyn_list::~gcalc_dyn_list()
{
  cleanup();
}


void gcalc_dyn_list::reset()
{
  item *pi_end, *cur_pi;
  *m_blk_hook= NULL;
  if (m_first_blk)
  {
    free_blk_list(*((void **)m_first_blk));
    m_blk_hook= (void**)m_first_blk;
    cur_pi= (item *)(((char *)m_first_blk) + PH_DATA_OFFSET);
    pi_end= ptr_add(cur_pi, m_points_per_blk);
    m_free= cur_pi;
    while (cur_pi<pi_end)
      cur_pi= cur_pi->next= ptr_add(cur_pi, 1);
    cur_pi->next= NULL;
  }
}


/* should be removed
void gcalc_heap::free_point_info(gcalc_heap::info **pi_hook)
{
  DBUG_ASSERT(m_n_points);
  gcalc_heap::info *pi= *pi_hook;
  *pi_hook= pi->next;
  free_item(pi);
  --m_n_points;
}
*/


static inline void trim_node(gcalc_heap::info *node, gcalc_heap::info *prev_node)
{
  if (!node)
    return;
  DBUG_ASSERT((node->left == prev_node) || (node->right == prev_node));
  if (node->left == prev_node)
    node->left= node->right;
  node->right= NULL;
}


static double find_first_different(const gcalc_heap::info *p)
{
  if (p->left && (p->left->y != p->y))
    return p->left->y;
  if (p->right && (p->right->y != p->y))
    return p->right->y;
  if (p->left && p->left->left && (p->left->left->y != p->y))
    return p->left->left->y;
  if (p->right && p->right->right && (p->right->right->y != p->y))
    return p->right->right->y;

  return p->y;
}


static int compare_point_info(const void *e0, const void *e1)
{
  const gcalc_heap::info *i0= (const gcalc_heap::info *)e0;
  const gcalc_heap::info *i1= (const gcalc_heap::info *)e1;
  if (i0->y != i1->y)
    return i0->y > i1->y;
  return find_first_different(i0) > find_first_different(i1);
}


void gcalc_heap::prepare_operation()
{
  DBUG_ASSERT(m_hook);
  *m_hook= NULL;
  m_first= sort_list(compare_point_info, m_first, m_n_points);
  m_hook= NULL; /* just to check it's not called twice */

  /* TODO - move this to the 'normal_scan' loop */
  for (info *cur= get_first(); cur; cur= cur->get_next())
  {
    trim_node(cur->left, cur);
    trim_node(cur->right, cur);
  }
#ifdef GCALC_DBUG
  {
    info *cur= get_first();
    GCALC_DBUG_PRINT(("shape\t  x\t  y\tleft\tright\tthis\n"));
    for (; cur; cur= cur->get_next())
    {
      GCALC_DBUG_PRINT(("%d\t%.15g\t%.15g\t %d\t %d\t %d\n", cur->shape,
                        cur->x, cur->y,(int)cur->left, (int)cur->right, (int) cur));
    }
    GCALC_DBUG_PRINT(("------------------\n"));
  }
#endif /*GCALC_DBUG*/
}


void gcalc_heap::reset()
{
  if (!m_hook)
  {
    m_hook= &m_first;
    for (; *m_hook; m_hook= &(*m_hook)->next);
  }

  *m_hook= m_free;
  m_free= m_first;
  m_hook= &m_first;
  m_n_points= 0;
}

int gcalc_shape_transporter::int_single_point(gcalc_shape_info info,
                                              double x, double y)
{
  gcalc_heap::info *point= m_heap->new_point_info(x, y, info);
  if (!point)
    return 1;
  point->left= point->right= 0;
  return 0;
}


int gcalc_shape_transporter::int_add_point(gcalc_shape_info info,
                                           double x, double y)
{
  gcalc_heap::info *point;
  GCALC_DBUG_ADD_POINT(x, y);
  if (!(point= m_heap->new_point_info(x, y, info)))
    return 1;
  if (m_first)
  {
    m_prev->left= point;
    point->right= m_prev;
  }
  else
    m_first= point;
  m_prev= point;
  return 0;
}


void gcalc_shape_transporter::int_complete()
{
  DBUG_ASSERT(m_shape_started == 1 || m_shape_started == 3);
  GCALC_DBUG_COMPLETE;

  if (!m_first)
    return;

  /* simple point */
  if (m_first == m_prev)
  {
    m_first->right= m_first->left= NULL;
    return;
  }

  /* line */
  if (m_shape_started == 1)
  {
    m_first->right= NULL;
    m_prev->left= m_prev->right;
    m_prev->right= NULL;
    return;
  }

  /* polygon */
  m_first->right= m_prev;
  m_prev->left= m_first;
}


inline int GET_DX_DY(double *dxdy,
                     const gcalc_heap::info *p0, const gcalc_heap::info *p1)
{
  double dy= p1->y - p0->y;
  *dxdy= p1->x - p0->x;
  return (dy == 0.0) ||
         (*dxdy/= dy)>DBL_MAX ||
         (*dxdy)<-DBL_MAX;
}

gcalc_scan_iterator::gcalc_scan_iterator(size_t blk_size) :
  gcalc_dyn_list(blk_size,
	         (sizeof(point) > sizeof(intersection)) ?
	          sizeof(point) : sizeof(intersection)),
  m_slice0(NULL), m_slice1(NULL)
{}
		  
gcalc_scan_iterator::point
  *gcalc_scan_iterator::new_slice(gcalc_scan_iterator::point *example)
{
  point *result= NULL;
  gcalc_dyn_list::item **result_hook= (gcalc_dyn_list::item **)&result;
  while (example)
  {
    *result_hook= new_slice_point();
    result_hook= &(*result_hook)->next;
    example= example->get_next();
  }
  *result_hook= NULL;
  return result;
}


void gcalc_scan_iterator::init(gcalc_heap *points)
{
  DBUG_ASSERT(points->ready());
  DBUG_ASSERT(!m_slice0 && !m_slice1);

  if (!(m_cur_pi= points->get_first()))
    return;
  m_cur_thread= 0;
  m_sav_slice= NULL;
  m_intersections= NULL;
  m_cur_intersection= NULL;
  m_y1= m_cur_pi->y;
  m_next_is_top_point= true;
  m_bottom_points_count= 0;
}

void gcalc_scan_iterator::reset()
{
  if (m_slice0)
    free_list(m_slice0);
  if (m_slice1)
    free_list(m_slice1);
  m_slice0= m_slice1= NULL;
  gcalc_dyn_list::reset();
}

static bool slice_first_equal_x(const gcalc_scan_iterator::point *p0,
				const gcalc_scan_iterator::point *p1)
{
  if (p0->horiz_dir == p1->horiz_dir)
    return p0->dx_dy <= p1->dx_dy;
  if (p0->horiz_dir)
    return p0->dx_dy < 0;
  return p1->dx_dy > 0;  /* p1->horiz_dir case */
}


static inline bool slice_first(const gcalc_scan_iterator::point *p0,
			       const gcalc_scan_iterator::point *p1)
{
  if (p0->x != p1->x)
    return p0->x < p1->x;
  return slice_first_equal_x(p0, p1);
}


int gcalc_scan_iterator::insert_top_point()
{
  point *sp= m_slice1;
  gcalc_dyn_list::item **prev_hook= (gcalc_dyn_list::item **)&m_slice1;
  point *sp1;
  point *sp0= new_slice_point();

  if (!sp0)
    return 1;
  sp0->pi= m_cur_pi;
  sp0->next_pi= m_cur_pi->left;
  sp0->thread= m_cur_thread++;
  sp0->x= coord_to_float(m_cur_pi->x);
  if (m_cur_pi->left)
  {
    sp0->horiz_dir= GET_DX_DY(&sp0->dx_dy, m_cur_pi, m_cur_pi->left);
    m_event1= scev_thread;

    /*Now just to increase the size of m_slice0 to be same*/
    if (!(sp1= new_slice_point()))
      return 1;
    sp1->next= m_slice0;
    m_slice0= sp1;
  }
  else
    m_event1= scev_single_point;

  /* First we need to find the place to insert.
     Binary search could probably make things faster here,
     but structures used aren't suitable, and the
     scan is usually not really long */
  for (; sp && slice_first(sp, sp0);
       prev_hook= &sp->next, sp=sp->get_next());

  if (m_cur_pi->right)
  {
    m_event1= scev_two_threads;
    /*We have two threads so should decide which one will be first*/
    sp1= new_slice_point();
    if (!sp1)
      return 1;
    sp1->pi= m_cur_pi;
    sp1->next_pi= m_cur_pi->right;
    sp1->thread= m_cur_thread++;
    sp1->x= sp0->x;
    sp1->horiz_dir= GET_DX_DY(&sp1->dx_dy, m_cur_pi, m_cur_pi->right);
    if (slice_first_equal_x(sp1, sp0))
    {
      point *tmp= sp0;
      sp0= sp1;
      sp1= tmp;
    }
    sp1->next= sp;
    sp0->next= sp1;
    
    /*Now just to increase the size of m_slice0 to be same*/
    if (!(sp1= new_slice_point()))
      return 1;
    sp1->next= m_slice0;
    m_slice0= sp1;
  }
  else
    sp0->next= sp;

  *prev_hook= sp0;
  m_event_position1= sp0;

  return 0;
}

enum
{
  intersection_normal= 1,
  intersection_forced= 2
};


static int intersection_found(const gcalc_scan_iterator::point *sp0,
			      const gcalc_scan_iterator::point *sp1,
			      unsigned int bottom_points_count)
{
  if (sp1->x < sp0->x)
    return intersection_normal;
  if (sp1->is_bottom() && !sp0->is_bottom() &&
      (bottom_points_count > 1))
      return intersection_forced;
  return 0;
}


int gcalc_scan_iterator::normal_scan()
{
  if (m_next_is_top_point)
    if (insert_top_point())
      return 1;

  point *tmp= m_slice0;
  m_slice0= m_slice1;
  m_slice1= tmp;
  m_event0= m_event1;
  m_event_position0= m_event_position1;
  m_y0= m_y1;
  
  if (!(m_cur_pi= m_cur_pi->get_next()))
  {
    free_list(m_slice1);
    m_slice1= NULL;
    return 0;
  }
  
  gcalc_heap::info *cur_pi= m_cur_pi;
  m_y1= coord_to_float(cur_pi->y);
  m_h= m_y1 - m_y0;

  point *sp0= m_slice0;
  point *sp1= m_slice1;
  point *prev_sp1= NULL;

  m_bottom_points_count= 0;
  m_next_is_top_point= true;
  bool intersections_found= false;

  for (; sp0; sp0= sp0->get_next())
  {
    if (sp0->next_pi == cur_pi) /* End of the segment */
    {
      sp1->x= coord_to_float(cur_pi->x);
      sp1->pi= cur_pi;
      sp1->thread= sp0->thread;
      sp1->next_pi= cur_pi->left;
      if (cur_pi->left)
	sp1->horiz_dir= GET_DX_DY(&sp1->dx_dy, m_cur_pi, m_cur_pi->left);

      m_next_is_top_point= false;
      
      if (sp1->is_bottom())
      {
	++m_bottom_points_count;
	if (m_bottom_points_count == 1)
	{
	  m_event1= scev_end;
	  m_event_position1= sp1;
	}
	else
	  m_event1= scev_two_ends;
      }
      else
      {
	m_event1= scev_point;
	m_event_position1= sp1;
      }
    }
    else if (!sp0->is_bottom())
    {
      /* Cut current string with the height of the new point*/
      sp1->copy_core(sp0);
      sp1->x= sp1->horiz_dir ? sp0->x :
	(coord_to_float(sp1->pi->x) +
	 (m_y1-coord_to_float(sp1->pi->y)) * sp1->dx_dy);
    }
    else  /* Skip the bottom point in slice0 */
      continue;

    intersections_found= intersections_found ||
      (prev_sp1 && intersection_found(prev_sp1, sp1, m_bottom_points_count));

    prev_sp1= sp1;
    sp1= sp1->get_next();
  }

  if (sp1)
  {
    if (prev_sp1)
      prev_sp1->next= NULL;
    else
      m_slice1= NULL;
    free_list(sp1);
  }

  GCALC_DBUG_PRINT(("Y%.15g", m_y0));
  GCALC_DBUG_SLICE(m_slice0);
  GCALC_DBUG_PRINT(("Y%.15g", m_y1));
  GCALC_DBUG_SLICE(m_slice1);
  GCALC_DBUG_PRINT(("\n"));

  if (intersections_found)
    return handle_intersections();

  return 0;
}


int gcalc_scan_iterator::add_intersection(const point *a, const point *b,
				   int isc_kind, gcalc_dyn_list::item ***p_hook)
{
  intersection *isc= new_intersection();

  if (!isc)
    return 1;
  m_n_intersections++;
  **p_hook= isc;
  *p_hook= &isc->next;
  isc->thread_a= a->thread;
  isc->thread_b= b->thread;
  if (isc_kind == intersection_forced)
  {
    isc->y= m_y1;
    isc->x= a->x;
    return 0;
  }

  /* intersection_normal */
  const point *a0= a->precursor;
  const point *b0= b->precursor;
  if (!a0->horiz_dir && !b0->horiz_dir)
  {
    double dk= a0->dx_dy - b0->dx_dy;
    double dy= (b0->x - a0->x)/dk;
    isc->y= m_y0 + dy;
    isc->x= a0->x + dy*a0->dx_dy;
    return 0;
  }
  isc->y= m_y1;
  isc->x= a0->horiz_dir ? b->x : a->x;
  return 0;
}


int gcalc_scan_iterator::find_intersections()
{
  point *sp1= m_slice1;
  gcalc_dyn_list::item **hook;

  m_n_intersections= 0;
  {
    /* Set links between slicepoints */
    point *sp0= m_slice0;
    for (; sp1; sp0= sp0->get_next(),sp1= sp1->get_next())
    {
      while (sp0->is_bottom())
	sp0= sp0->get_next();
      DBUG_ASSERT(sp0->thread == sp1->thread);
      sp1->precursor= sp0;
    }
  }

  hook= (gcalc_dyn_list::item **)&m_intersections;
  bool intersections_found;

  point *last_possible_isc= NULL;
  do
  {
    sp1= m_slice1;
    point **pprev_s1= &m_slice1;
    intersections_found= false;
    unsigned int bottom_points_count= sp1->is_bottom() ? 1:0;
    sp1= m_slice1->get_next();
    int isc_kind;
    point *cur_possible_isc= NULL;
    for (; sp1 != last_possible_isc;
	 pprev_s1= (point **)(&(*pprev_s1)->next), sp1= sp1->get_next())
    {
      if (sp1->is_bottom())
	++bottom_points_count;
      if (!(isc_kind=intersection_found(*pprev_s1, sp1, bottom_points_count)))
	continue;
      point *prev_s1= *pprev_s1;
      intersections_found= true;
      if (add_intersection(prev_s1, sp1, isc_kind, &hook))
	return 1;
      *pprev_s1= sp1;
      prev_s1->next= sp1->next;
      sp1->next= prev_s1;
      sp1= prev_s1;
      cur_possible_isc= sp1;
    }
    last_possible_isc= cur_possible_isc;
  } while (intersections_found);

  *hook= NULL;
  GCALC_DBUG_PRINT(("intersections found:%d\n", m_n_intersections));
  return 0;
}


static int compare_intersections(const void *e0, const void *e1)
{
  gcalc_scan_iterator::intersection *i0= (gcalc_scan_iterator::intersection *)e0;
  gcalc_scan_iterator::intersection *i1= (gcalc_scan_iterator::intersection *)e1;
  return i0->y > i1->y;
}


inline void gcalc_scan_iterator::sort_intersections()
{
  m_intersections= (intersection *)sort_list(compare_intersections,
                                             m_intersections,m_n_intersections);
}


int gcalc_scan_iterator::handle_intersections()
{
  DBUG_ASSERT(m_slice1->next);

  if (find_intersections())
    return 1;
  sort_intersections();

  m_sav_slice= m_slice1;
  m_sav_y= m_y1;
  m_slice1= new_slice(m_sav_slice);
  
  m_cur_intersection= m_intersections;
  m_pre_intersection_hook= NULL;
  return intersection_scan();
}


void gcalc_scan_iterator::pop_suitable_intersection()
{
  intersection *prev_i= m_cur_intersection;
  intersection *cur_i= prev_i->get_next();
  for (; cur_i; prev_i= cur_i, cur_i= cur_i->get_next())
  {
    point *prev_p= m_slice0;
    point *sp= prev_p->get_next();
    for (; sp; prev_p= sp, sp= sp->get_next())
    {
      if ((prev_p->thread == cur_i->thread_a) &&
	  (sp->thread == cur_i->thread_b))
      {
	/* Move cur_t on the top of the list */
	if (prev_i == m_cur_intersection)
	{
	  m_cur_intersection->next= cur_i->next;
	  cur_i->next= m_cur_intersection;
	  m_cur_intersection= cur_i;
	}
	else
	{
          gcalc_dyn_list::item *tmp= m_cur_intersection->next;
	  m_cur_intersection->next= cur_i->next;
	  prev_i->next= m_cur_intersection;
	  m_cur_intersection= cur_i;
	  cur_i->next= tmp;
	}
	return;
      }
    }
  }
  DBUG_ASSERT(0);
}


int gcalc_scan_iterator::intersection_scan()
{
  if (m_pre_intersection_hook) /*Skip the first point*/
  {
    point *next= (*m_pre_intersection_hook)->get_next();
    (*m_pre_intersection_hook)->next= next->next;
    next->next= *m_pre_intersection_hook;
    *m_pre_intersection_hook= next;
    m_event0= scev_intersection;
    m_event_position0= next;
    point *tmp= m_slice1;
    m_slice1= m_slice0;
    m_slice0= tmp;
    m_y0= m_y1;
    m_cur_intersection= m_cur_intersection->get_next();
    if (!m_cur_intersection)
    {
      m_h= m_sav_y - m_y1;
      m_y1= m_sav_y;
      free_list(m_slice1);
      m_slice1= m_sav_slice;
      free_list(m_intersections);
      GCALC_DBUG_PRINT(("  is Y%.15g", m_y0));
      GCALC_DBUG_SLICE(m_slice0);
      GCALC_DBUG_PRINT(("  is Y%.15g", m_y1));
      GCALC_DBUG_SLICE(m_slice1);
      GCALC_DBUG_PRINT(("\n"));
      return 0;
    }
  }

  m_y1= m_cur_intersection->y;
  m_h= m_y1 - m_y0;

  point *sp0;
  point **psp1;

redo_loop:
  sp0= m_slice0;
  psp1= &m_slice1;
  for (; sp0; sp0= sp0->get_next())
  {
    point *sp1= *psp1;
    if (sp0->thread == m_cur_intersection->thread_a)
    {
      point *next_s0= sp0;
      /* Skip Bottom points */
      do
	next_s0= next_s0->get_next();
      while(next_s0->is_bottom()); /* We always find nonbottom point here*/
      /* If the next point's thread isn't the thread of intersection,
	 we try to find suitable intersection */
      if (next_s0->thread != m_cur_intersection->thread_b)
      {
	/* It's really rare case - sometimes happen when
	   there's two intersections with the same Y
	   Move suitable one to the beginning of the list
	*/
        GCALC_DBUG_PRINT(("redo_loop needed\n"));
	pop_suitable_intersection();
	goto redo_loop;
      }
      m_pre_intersection_hook= psp1;
      sp1->copy_core(sp0);
      sp1->x= m_cur_intersection->x;
      sp0= next_s0;
      sp1= sp1->get_next();
      sp1->copy_core(sp0);
      sp1->x= m_cur_intersection->x;
      psp1= (point **)&sp1->next;
      continue;
    }
    if (!sp0->is_bottom())
    {
      sp1->copy_core(sp0);
      sp1->x= sp1->horiz_dir ? sp0->x :
	(coord_to_float(sp1->pi->x) +
	 (m_y1-coord_to_float(sp1->pi->y)) * sp1->dx_dy);
    }
    else
      /* Skip bottom point */
      continue;
    psp1= (point **)&sp1->next;
  }

  if (*psp1)
  {
    free_list(*psp1);
    *psp1= NULL;
  }

  GCALC_DBUG_PRINT(("  is Y%.15g", m_y0));
  GCALC_DBUG_SLICE(m_slice0);
  GCALC_DBUG_PRINT(("  is Y%.15g", m_y1));
  GCALC_DBUG_SLICE(m_slice1);
  GCALC_DBUG_PRINT(("\n"));
  return 0;
}

#endif /* HAVE_SPATIAL */
