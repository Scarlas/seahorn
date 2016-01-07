#include "seahorn/Bmc.hh"
#include "seahorn/UfoSymExec.hh"

namespace seahorn
{
  /// computes an implicant of f (interpreted as a conjunction) that
  /// contains the given model
  static void get_model_implicant (const ExprVector &f, 
                                   ufo::ZModel<ufo::EZ3> &model, ExprVector &out);
    
  void BmcEngine::addCutPoint (const CutPoint &cp)
  {
    if (m_cps.empty ())
    {
      m_cpg = &cp.parent ();
      m_fn = cp.bb ().getParent ();
    }
  
    assert (m_cpg == &cp.parent ());
    m_cps.push_back (&cp);
  }

  boost::tribool BmcEngine::solve ()
  {
    encode ();
    m_result =  m_smt_solver.solve ();
    return m_result;
  }

  void BmcEngine::encode ()
  {
    
    // -- only run the encoding once
    if (!m_side.empty ()) return;
    
    assert (m_cpg);
    assert (m_fn);
    UfoLargeSymExec sexec (m_sem);
    m_states.push_back (SymStore (m_efac));
    
    const CutPoint *prev = &m_cpg->getCp (m_fn->getEntryBlock ());
    for (const CutPoint *cp : m_cps)
    {
      m_states.push_back (m_states.back ());
      SymStore &s = m_states.back ();
      
      const CpEdge *edg = m_cpg->getEdge (*prev, *cp);
      assert (edg);
      m_edges.push_back (edg);
      sexec.execCpEdg (s, *edg, m_side);
      
      prev = cp;
    }
    
    for (Expr v : m_side) m_smt_solver.assertExpr (v);
    
  }

  void BmcEngine::reset ()
  {
    m_cps.clear ();
    m_cpg = nullptr;
    m_fn = nullptr;
    m_smt_solver.reset ();

    m_side.clear ();
    m_states.clear ();
    m_edges.clear ();
  }
  
  
  void BmcEngine::unsatCore (ExprVector &out)
  {
    // -- re-assert the path-condition with assumptions
    m_smt_solver.reset ();
    ExprVector assumptions;
    assumptions.reserve (m_side.size ());
    for (Expr v : m_side)
    {
      Expr a = bind::boolConst (mk<ASM> (v));
      assumptions.push_back (a);
      m_smt_solver.assertExpr (mk<IMPL> (a, v));
    }
    
    ExprVector core;
    m_smt_solver.push ();
    boost::tribool res = m_smt_solver.solveAssuming (assumptions);
    if (!res) m_smt_solver.unsatCore (std::back_inserter (core));
    m_smt_solver.pop ();
    if (res) return;

    
    // simplify core
    while (core.size () < assumptions.size ())
    {
      assumptions.assign (core.begin (), core.end ());
      core.clear ();
      m_smt_solver.push ();
      res = m_smt_solver.solveAssuming (assumptions);
      assert (!res ? 1 : 0);
      m_smt_solver.unsatCore (std::back_inserter (core));
      m_smt_solver.pop ();
    }    
    
    // minimize simplified core
    for (unsigned i = 0; i < core.size ();)
    {
      Expr saved = core [i];
      core [i] = core.back ();
      res = m_smt_solver.solveAssuming 
        (boost::make_iterator_range (core.begin (), core.end () - 1));
      if (res) core [i++] = saved;
      else if (!res)
        core.pop_back ();
      else assert (0);
    }
    
    // unwrap the core from ASM to corresponding expressions
    for (Expr c : core)
      out.push_back (bind::fname (bind::fname (c))->arg (0));
  }
  
  static void get_model_implicant (const ExprVector &f, 
                                   ufo::ZModel<ufo::EZ3> &model, ExprVector &out)
  {
    // XXX This is a partial implementation. Specialized to the
    // constraints expected to occur in m_side.
    
    for (auto v : f)
    {
      // -- break IMPL into an OR
      // -- OR into a single disjunct
      // -- single disjunct into an AND
      if (isOpX<IMPL> (v))
      {
        assert (v->arity () == 2);
        Expr a0 = model (v->arg (0));
        if (isOpX<FALSE> (a0)) continue;
        else if (isOpX<TRUE> (a0))
          v = mknary<OR> (mk<FALSE> (a0->efac ()), 
                          ++(v->args_begin ()), v->args_end ());
        else
          continue;
      }
      
      if (isOpX<OR> (v))
      {
        for (unsigned i = 0; i < v->arity (); ++i)
          if (isOpX<TRUE> (model (v->arg (i))))
          {
            v = v->arg (i);
            break;
          }
      }
        
      if (isOpX<AND> (v)) 
      {
        for (unsigned i = 0; i < v->arity (); ++i)
          out.push_back (v->arg (i));
      }
      else out.push_back (v);
    }      
    
  }

}
