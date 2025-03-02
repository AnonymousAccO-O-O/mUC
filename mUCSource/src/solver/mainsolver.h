#ifndef NEW_MAIN_SOLVER_H
#define NEW_MAIN_SOLVER_H

#include "carsolver.h"
#include "data_structure.h"
#include "model.h"
#include "statistics.h"
#include <vector>
#include <assert.h>
#include <iostream>

namespace car
{
	extern Statistics CARStats;

	class MainSolver : public CARSolver
	{
	public:
		MainSolver(Model *, int rotate_is_on, const bool verbose = false, bool uc_no_sort = false);
		MainSolver (Model* m, const bool verbose, int unroll_level, bool placeholder) ;
		~MainSolver() {}

		void set_assumption(const Assignment &, const int);
		
		// set assumption = { s->s() , flag_of(Os[frame_level]) }
		void set_assumption(Osequence *O, State *s, const int frame_level, const bool forward);
		void set_assumption(Osequence *O, State *s, const int frame_level, const bool forward, const std::vector<Cube>& prefers);
		
        // if assumptions are already set, just solve.
		inline bool solve_with_assumption(){return CARSolver::solve_assumption();};
		bool solve_with_assumption(const Assignment &assump, const int p);

		// @note: can only be used if the unroll level is 1
		State* get_state(const bool forward);

		void get_states(std::vector<State*>& states, const bool forward);
		Assignment get_state_full_assignment(const bool forward);

		// this version is used for bad check only
		Cube get_conflict_no_bad(const int bad);
		Cube get_conflict(const bool forward);
        Cube get_conflict_another(const bool forward);

		inline void add_cube_negate(Cube &cu)
		{
			CARSolver::add_cube_negate(cu);
		}

		void add_new_frame(const Frame& frame, const int frame_level, Osequence *O, const bool forward);

		void add_clause_from_cube(const Cube &cu, const int frame_level, Osequence *O, const bool forward);

		void shrink_model(Assignment &model);

	public:
		// This section is for bi-car

		// silly method: clear the main solver when seraching with the O sequence ends.
		// clever method: record flag of different levels in each O sequence.
		
		static std::unordered_map<Osequence*,std::vector<int>> flag_of_O;

		inline int flag_of(Osequence *o, const int frame_level)
		{
			assert(frame_level >= 0);
			while(frame_level >= flag_of_O[o].size())
			{
				flag_of_O[o].push_back(max_flag++);
			}
			return flag_of_O[o][frame_level];
		}
		
		void bi_record_O(Osequence *o, bool dir)
		{
			// should not have met before.
			assert(flag_of_O.count(o) == 0); 
			flag_of_O[o] = {};
		}

	public:
		// unroll relevant functions
		inline int lits_per_round()
		{
			// flag for each level, with the first layer's flag remaining unused
			return model_->max_id() + 1;
		}
		
        // NOTE: this is deprecated for now, because we only pop one frame flag at present.
		void unroll();

		void enable_level(int level);
		void enable_max();

		int unroll_level;
	private:
		Model *model_;
		int max_flag;
        bool rotate_is_on;
        bool uc_no_sort;
	};

}

#endif
