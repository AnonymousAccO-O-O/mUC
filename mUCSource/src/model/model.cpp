#include "model.h"
#include "utility.h"
#include <stdlib.h>
#include <iostream>
#include <assert.h>

using namespace std;

namespace car{

	Model::Model (aiger* aig, const bool verbose)
	{
		verbose_ = verbose;
		// According to aiger format, inputs should be [1 ... num_inputs_]
		// and latches should be [num_inputs+1 ... num_latches+num_inputs]]
		num_inputs_ = aig->num_inputs;
		num_latches_ = aig->num_latches;
		num_ands_ = aig->num_ands;
		num_constraints_ = aig->num_constraints;
		num_outputs_ = aig->num_outputs;

		//preserve two more ids for TRUE (max_id_ - 1) and FALSE (max_id_)
		max_id_ = aig->maxvar+2;
		max_model_id_ =  aig->maxvar+2;
		true_ = max_id_ - 1;
		false_ = max_id_;
		
		collect_trues (aig);
		
		set_constraints (aig);
		set_outputs (aig);
		set_init (aig);
		
		create_next_map (aig);
		create_clauses (aig);
	}
	
	// collect those that is trivially constant
	void Model::collect_trues (const aiger* aig)
	{
		for (int i = 0; i < aig->num_ands; i ++)
		{
			aiger_and& aa = aig->ands[i];
			// lhs of an and gate is always even in aiger
			assert (aa.lhs % 2 == 0);
			if (is_true (aa.rhs0) && is_true (aa.rhs1))
				trues_.insert (aa.lhs);
			else if (is_false (aa.rhs0) || is_false (aa.rhs1))
				trues_.insert (aa.lhs + 1);
			// lhs = 8 * 7 = a4 * ~a3 is not constant
			// lhs = 9 * 8 = ~a4 * a4 is constant
			else if (aa.rhs0 == aa.rhs1+1 && aa.rhs0 % 2 == 1)
				trues_.insert (aa.lhs + 1);
		}
	}

	void Model::set_constraints (const aiger* aig)
	{
		for (int i = 0; i < aig->num_constraints; i++)
		{
			int lit = (int)aig->constraints[i].lit;
			constraints_.push_back(car_var(lit));
		}
	}
	
	void Model::set_outputs (const aiger* aig)
	{
		for (int i = 0; i < aig->num_outputs; i++)
		{
			int lit = (int)aig->outputs[i].lit;
			outputs_.push_back(car_var(lit));
		}
	}

	void Model::set_init (const aiger* aig)
	{
		for (int i = 0; i < aig->num_latches; i ++)
		{
			if (aig->latches[i].reset == 0)
				init_.push_back (-(num_inputs_+1+i));
			else if (aig->latches[i].reset == 1)
				init_.push_back (num_inputs_+1+i);
			else
			{
				cout << "Error setting initial state!" << endl;
				exit (0);
			}
		}
	}
	
	void Model::create_next_map (const aiger* aig)
	{
		for (int i = 0; i < aig->num_latches; i ++)
		{
			int val = (int)aig->latches[i].lit;
			//a latch should not be a negative number
			assert (val % 2 == 0);
			val = val / 2;
			//make sure our assumption about latches is correct
			assert (val == (num_inputs_ + 1 + i));
			
			//pay attention to the special case when next_val = 0 or 1
			if (is_false (aig->latches[i].next))  //FALSE
			{
				next_map_.insert (std::pair<int, int> (val, false_));
				insert_to_reverse_next_map (false_, val);
			}
			else if (is_true (aig->latches[i].next)) //TRUE
			{
				next_map_.insert (std::pair<int, int> (val, true_));
				insert_to_reverse_next_map (true_, val);
			}
			else
			{
				int next_val = (int) aig->latches[i].next;
				next_val = (next_val % 2 == 0) ? (next_val/2) : -(next_val/2);
				next_map_.insert (std::pair<int, int> (val, next_val));
				insert_to_reverse_next_map (abs (next_val), (next_val > 0) ? val : -val);
			}
			
		}
	}
	
	void Model::insert_to_reverse_next_map (const int index, const int val)
	{
	    reverseNextMap::iterator it = reverse_next_map_.find (index);
	    if (it == reverse_next_map_.end ())
	    {
	        vector<int> v;
	        v.push_back (val);
	        reverse_next_map_.insert (std::pair<int, vector<int> > (index, v));
	    }
	    else
	        (it->second).push_back (val);
	}
	
	void Model::create_clauses (const aiger* aig)
	{
		// contraints, outputs and latches gates are stored in order,
		// as the need for start solver construction
		std::unordered_set<unsigned> exist_gates, gates;


		// ============================================================================
		// (1) create clauses for constraints encoding
		// ============================================================================
		collect_necessary_gates(aig, aig->constraints, aig->num_constraints, exist_gates, gates);

		for (std::unordered_set<unsigned>::iterator it = gates.begin(); it != gates.end(); it++)
		{
			aiger_and *aa = aiger_is_and(const_cast<aiger *>(aig), *it);
			assert(aa != NULL);
			add_clauses_from_equation(aa);
		}

		// ============================================================================
		// (2) same next have same previous, initialized to 0.
		// ============================================================================
		// if several latches' next value points to a same one, the values of these latches also have the constraint
		// But NOTE that all states in the aiger model must satisfy this constraint, EXCEPT the initial state
		// Also it is needed to add new contraints later
		

		set_common_next_start();
		// @note: even if no gates share a common next, the helper lit will be inserted.
		create_constraints_for_latches();

		// ============================================================================
		// (3) clause for encoding outputs.
		// ============================================================================
		// create clauses for outputs

		set_outputs_start();

		gates.clear();
		//  Use Outputs as the start point, recursively add all the rhs lits.
		collect_necessary_gates(aig, aig->outputs, aig->num_outputs, exist_gates, gates);
		for (std::unordered_set<unsigned>::iterator it = gates.begin(); it != gates.end(); it++)
		{
			aiger_and *aa = aiger_is_and(const_cast<aiger *>(aig), *it);
			assert(aa != NULL);
			add_clauses_from_equation(aa);
		}

		// ============================================================================
		// (4) clause for encoding latches's mapping relation
		// ============================================================================
		// create clauses for latches

		set_latches_start();

		//  Use Next value of Latches as the start point, recursively add all the rhs lits.
		gates.clear();
		collect_necessary_gates(aig, aig->latches, aig->num_latches, exist_gates, gates, true);
		for (std::unordered_set<unsigned>::iterator it = gates.begin(); it != gates.end(); it++)
		{
			aiger_and *aa = aiger_is_and(const_cast<aiger *>(aig), *it);
			assert(aa != NULL);
			add_clauses_from_equation(aa);
		}

		// ============================================================================
		//// (5) create clauses for true and false
		// ============================================================================
		cls_.push_back(clause(true_));
		cls_.push_back(clause(-false_));
	}

	/**
	 * @brief 
	 * flag1: All latches that share a common next have same value 
	 * flag2: All latches are initialized to 0
	 * flags are enabled when assigned to 1.
	 */
	void Model::create_constraints_for_latches()
	{
		int flag1 = ++max_id_;

		bool exist = false;
		for (reverseNextMap::iterator it = reverse_next_map_.begin(); it != reverse_next_map_.end(); it++)
		{
			vector<int> &v = it->second;
			if (v.size() <= 1)
				continue;
			/**
			 * @brief As to latches { (l1,n), (l2,n), (l3, n) }
			 * we assume val(l1) == val(l2) == val(l3)
			 * which means, l1 <-> l2 <-> l3
			 * <=> (l1, ~l2), (~l1, l2), (l2, ~l3), (~l2, l3)
			 */
			// TODO: Try to record this equivalence relationship using a map and simplify the model.
			exist = true;
			for (int i = 0; i < v.size() - 1; i++)
			{
				cls_.push_back({v[i], -v[i + 1], -flag1});
				cls_.push_back({-v[i], v[i + 1], -flag1});
			}
		}
		if (!exist)
		{
			// at last add one clause for flag1 so that the sat solver will not treat flag1 as one literal
			cls_.push_back({++max_id_, -flag1});
		}

		// add initial state
		int flag2 = ++max_id_;
		for (int i = 0; i < init_.size(); i++)
		{
			cls_.push_back({init_[i], -flag2});
		}
		// either : all latches share a common next
		// or : this is the initial states, all latches are initialized.
		cls_.push_back({flag1, flag2});
	}

	void Model::collect_necessary_gates (const aiger* aig, const aiger_symbol* as, const int as_size, 
	                                        std::unordered_set<unsigned>& exist_gates, std::unordered_set<unsigned>& gates, bool next)
	{
		for (int i = 0; i < as_size; i ++)
		{
			aiger_and* aa;
			if (next) 
			    aa = necessary_gate (as[i].next, aig);
			else
			{
			    aa = necessary_gate (as[i].lit, aig);
			    if (aa == NULL)
			    {
			    	if (is_true (as[i].lit))
			    		outputs_[i] = true_;
			    	else if (is_false (as[i].lit))
			    		outputs_[i] = false_;
			    }
			}
			recursively_add (aa, aig, exist_gates, gates);	
		}
		
	}
	
	aiger_and* Model::necessary_gate (const unsigned id, const aiger* aig)
	{
		if (!is_true (id) && !is_false (id))
			return aiger_is_and (const_cast<aiger*> (aig), (id % 2 == 0) ? id : (id-1));
			
		return NULL;
	}
	
	void Model::recursively_add (const aiger_and* aa, const aiger* aig, std::unordered_set<unsigned>& exist_gates, std::unordered_set<unsigned>& gates)
	{
		if (aa == NULL)
			return;
		if (exist_gates.find (aa->lhs) != exist_gates.end ())
			return;
		
		gates.insert (aa->lhs);
		exist_gates.insert (aa->lhs);
		aiger_and* aa0 = necessary_gate (aa->rhs0, aig);
		recursively_add (aa0, aig, exist_gates, gates);
		
		aiger_and* aa1 = necessary_gate (aa->rhs1, aig);
		recursively_add (aa1, aig, exist_gates, gates);
	}
	
	/**
	 * @brief 
	 * 
	 * @param aa 
	 */
	void Model::add_clauses_from_equation (const aiger_and* aa)
	{
		assert (aa != NULL);
		assert (!is_true (aa->lhs) && !is_false (aa->lhs));
		
		if (is_true (aa->rhs0))
		{
			/**
			 * @brief lhs = True /\ rhs1
			 * <=>	  lhs <-> rhs1
			 * <=>	  lhs -> rhs1 /\ rhs1 -> lhs
			 * <=>	  (~lhs \/ rhs1), (~rhs1, lhs)
			 */
			cls_.push_back (clause (car_var (aa->lhs), -car_var (aa->rhs1)));
			cls_.push_back (clause (-car_var (aa->lhs), car_var (aa->rhs1)));
		}
		else if (is_true (aa->rhs1))
		{
			cls_.push_back (clause (car_var (aa->lhs), -car_var (aa->rhs0)));
			cls_.push_back (clause (-car_var (aa->lhs), car_var (aa->rhs0)));
		}
		else
		{
			cls_.push_back (clause (car_var (aa->lhs), -car_var (aa->rhs0), -car_var (aa->rhs1)));
			cls_.push_back (clause (-car_var (aa->lhs), car_var (aa->rhs0)));
			cls_.push_back (clause (-car_var (aa->lhs), car_var (aa->rhs1)));
		}
			
	}
	
	/**
	 * @brief get the next id of given id
	 * 
	 * @param id 
	 * @return int 
	 */
	int Model::prime (const int id)
	{
		nextMap::iterator it = next_map_.find (abs (id));
		if (it == next_map_.end ())
		{
			cerr<<"[Fatal Error] Cannot find its prime:" << id<< endl;
			assert(false && "cannot find its prime");
		    return 0; //not found
		}
		return (id > 0 ? it->second : -(it->second));
	}
	
	/**
	 * @brief get the previous ids of given id
	 * @note there may be more than one.
	 * @param id 
	 * @return std::vector<int> 
	 */
	std::vector<int> Model::previous (const int id)
	{
	    vector<int> res;
	    reverseNextMap::iterator it = reverse_next_map_.find (abs (id));
		if (it == reverse_next_map_.end ())
		    return res; //not found
		res = it->second;
		if (id < 0)
		{
		    for (int i = 0; i < res.size (); i ++)
		        res[i] = -res[i];
		}
		return res;
	}
	
	/**
	 * @brief collect the previous vars from uc.  
	 * @note multiple latch may share a common next, therefore one lit may have several previous.
	 * @param uc 
	 */
	void Model::shrink_to_previous_vars (Cube& uc)
	{
		Cube tmp;
		for (int i = 0; i < uc.size (); i ++)
		{
		    vector<int> ids = previous (abs (uc[i]));
			if (ids.empty ())
			    continue;
			// 每一个pre都放进去。
			for (int j = 0; j < ids.size (); j ++)
				tmp.push_back ((uc[i] > 0) ? ids[j] : (-ids[j]));	
		}
		uc = tmp;
	}
	
	/**
	 * @brief collect the latch vars in uc.  
	 * @pre since we now set all the flags t obe at the beginning of the assumption, they shall be at the end.
	 * @param uc 
	 */
	void Model::shrink_to_latch_vars (Cube& uc)
	{
		if(!latch_var(abs(uc.back())))
        	uc.pop_back();
        // for(auto i :uc)
        // {
        //     assert(latch_var(abs(i)));
        // }
	}
	
	
	void Model::print ()
	{
	    cout << "-------------------Model information--------------------" << endl;
	    cout << endl << "number of clauses: " << cls_.size () << endl;
	    for (int i  = 0; i < cls_.size (); i ++)
	        car::print (cls_[i]);
	    cout << endl << "next map: " << endl;
	    car::print (next_map_);
	    cout << endl << "reverse next map:" << endl;
	    car::print (reverse_next_map_);
	    cout << endl << "Initial state:" << endl;
	    car::print (init_);
	    cout << endl << "number of Inputs: " << num_inputs_ << endl;
	    cout << endl << "number of Latches: " << num_latches_ << endl;
	    cout << endl << "number of Outputs: " << num_outputs_ << endl;
	    car::print (outputs_);
	    cout << endl << "number of constraints: " << num_constraints_ << endl;
	    car::print (constraints_);
	    cout << endl << "Max id used: " << max_id_ << endl;
	    cout << endl << "outputs start index: " << outputs_start_ << endl;
	    cout << endl << "latches start index: " << latches_start_ << endl;
	    cout << endl << "number of TRUE variables: " << trues_.size () << endl;
	    car::print (trues_); 
	    cout << endl << "-------------------End of Model information--------------------" << endl;   
	}
}
