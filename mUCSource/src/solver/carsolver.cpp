#include "carsolver.h"
#include <iostream>
#include <vector>
#include <algorithm>
using namespace std;
#ifdef MINISAT
	using namespace Minisat;
#else
	using namespace Glucose;
#endif // DEBUG

namespace car
{
 	
	/**
	 * @brief int -> SAT lit
	 * 
	 * @param id 
	 * @return Lit 
	 */
 	Lit CARSolver::SAT_lit (int id) 
 	{
 		assert (id != 0);
        int var = abs(id)-1;
        int varsNeeded = var - nVars() + 1; // how many needed
        while (varsNeeded > 0) {
            newVar();
            --varsNeeded;
        }
		return ( (id > 0) ? mkLit(var) : ~mkLit(var) );
 	}
 	
	/**
	 * @brief SAT lit -> int
	 * 
	 * @param l 
	 * @return int 
	 */
 	int CARSolver::lit_id (Lit l) const
	{
		if (sign(l)) 
            return -(var(l) + 1);
		else 
            return var(l) + 1;
	}
 	
	
	/**
	 * @brief get the assumption in _assumption
	 * 
	 * @return std::vector<int> 
	 */
	std::vector<int> CARSolver::get_assumption() const
	{
		std::vector<int> res;
		res.reserve(assumptions.size());
		for(int i = 0 ; i < assumptions.size();++i)
		{
			res.push_back(lit_id(assumptions[i]));
		}
		return std::move(res);
	}

	/**
	 * @brief return the model from SAT solver when it provides SAT
	 * 
	 * @return std::vector<int> : the result from Solver.
	 */
    // FIXME: do we need the whole? or just the original ones (excluding the labels and so on)
	std::vector<int> CARSolver::get_model () const
	{
		std::vector<int> res;
		res.resize (nVars (), 0);
   		for (int i = 0; i < nVars (); i ++)
   		{
     		if (model[i] == l_True)
       			res[i] = i+1;
     		else
       			res[i] = -(i+1);
   		}
   		return std::move(res);
	}

	/**
	 * 
	 * @brief return the UC from SAT solver when it provides UNSAT
	 * 
	 * @return std::vector<int> 
	 */
 	std::vector<int> CARSolver::get_uc () const
 	{
 		std::vector<int> reason;
		reason.resize(conflict.size(),0);
		// 
 		for (int k = 0; k < conflict.size(); k++) 
 		{
        	Lit l = conflict[k];
        	reason[k] = -lit_id (l);
    	}
    	return std::move(reason);
  	}
		
	/**
	 * @brief return the UC without bad from SAT solver(in particular, from `conflict`) when it provides UNSAT
	 * @todo TODO: use std::transform() may help improve the efficiency by parallel.
	 * @return std::vector<int> 
	 */
	std::vector<int> CARSolver::get_uc_no_bad (int bad) const
 	{
 		std::vector<int> reason;
 		for (int k = 0; k < conflict.size(); k++) 
 		{
        	Lit l = conflict[k];
			int id = -lit_id (l);
			if(id!=bad)
	        	reason.push_back (id);
    	}
    	return std::move(reason);
  	}

    /**
     * @brief Get another UC.
     * @pre Prior UC has been retrieved already.
     * @param forward 
     * @return Cube 
     */
    std::vector<int> CARSolver::get_uc_another()
    {
        /**
         * @brief previous assumption : <flag> <taken> <rest>
         * while <taken> == <uc> \Union <considered>
         */

        // <flag>. This is the flag of the target frame. It's easy.
        Lit flag = assumptions[0];
        int sz = assumptions.size();

        // cerr<<"old assumptions";
        // for(int i = 0; i< sz; ++i)
        //     cerr<<assumptions[i].x<<", ";
        // cerr<<endl;

        for(int i = 1; i <= sz/2; ++i)
        {
            // swap!
            int tmp = assumptions[i].x;
            assumptions[i].x = assumptions[sz-i].x;
            assumptions[sz-i].x = tmp;
            
        }
        
        // cerr<<"new assumptions";
        // for(int i = 0; i< sz; ++i)
        //     cerr<<assumptions[i].x<<", ";
        // cerr<<endl;
        solve_();

        return get_uc();
        
        

        // clever method? idk

        // Next, we try to identify between <taken> and <rest>
        // The distinction lies in that, whether they have been taken.
        // Though, if the decision level that finally triggered a conflict is larger than assumption size, there is no such rest.

        // The first lit in UC that comes after flag, is the splitter.
        // Let's see where it is in the assumptions.
        /*
        int index_taken = 0;
        for(; index_taken < assumptions.size(); ++index_taken)
        {
            // conflict[1], is the conflict literal.
            if(assumptions[index_taken] == conflict[1])
                break;
        }
        assert(index_taken < assumptions.size()); 

        // ...
        
        // <rest> <ignored> <UC>
        return std::vector<int>({});
        */
    }
	 	
	/**
	 * @brief 把cube中的每个元素作为一个clause加入。
	 * 
	 * @param cu 
	 */
 	void CARSolver::add_cube (const std::vector<int>& cu)
 	{
 	    for (int i = 0; i < cu.size (); i ++)
 	        add_clause (cu[i]);
 	}

	/**
	 * @brief 把cube中的每个元素取反，合并为一个clause。表示的是该cube的否定。
	 * 
	 * @param cu 
	 */
	void CARSolver::add_cube_negate (const std::vector<int>&cu)
	{
		vector<int> v = cu;
		for(int& i:v)
			i = -i;
		add_clause (v);
	}

	/**
	 * @brief put the vector into SAT Solver. (as a clause)
	 * 
	 * @param v 
	 */
	void CARSolver::add_clause_internal (const std::vector<int>& v)
 	{
 		vec<Lit> lits(v.size());
		int index = 0;
		for (int id : v)
			lits[index++] = SAT_lit(id);
 		bool res = addClause (lits);
		assert(res && "Warning: Adding clause does not success\n");
 	}

	/**
	 * @brief helper function, print last 3 clauses in the Solver.
	 * 
	 */
	void CARSolver::print_last_3_clauses()
	{
		cout << "Last 3 clauses in SAT solver: \n";
		int cnt = 0;
		for (int i = clauses.size () -1 ; i >=0; i--)
		{
			if(++cnt == 4)
				break;
			Clause& c = ca[clauses[i]];
			std::vector<int> vec;
			for (int j = 0; j < c.size (); j ++)
				vec.push_back(lit_id(c[j]));
			std::sort(vec.begin(),vec.end(),[](int a, int b){return abs(a) < abs(b);});
			for (int j = 0; j < vec.size (); j ++)
				cout<<vec[j]<<" ";
			
			cout << "0 " << endl;
		}
	}

	/**
	 * @brief helper function, print all the clauses in the Solver.
	 * 
	 */
 	void CARSolver::print_clauses(ostream & out)
	{
		out << "clauses in SAT solver: \n";
		for (int i = 0; i < clauses.size (); i ++)
		{
			Clause& c = ca[clauses[i]];
			for (int j = 0; j < c.size (); j ++)
				out << lit_id (c[j]) << " ";
			out << "0 " << endl;
		}
	}
	
	/**
	 * @brief helper function, print the assumption in the Solver.
	 * 
	 */
	void CARSolver::print_assumption (ostream & out)
	{
	    out << "assumptions in SAT solver: \n";
		if (!assumptions.size())
			out<<" Empty ";
	    for (int i = 0; i < assumptions.size (); i ++)
	        out << lit_id (assumptions[i]) << " ";
	    out << endl;
	}
	
}
