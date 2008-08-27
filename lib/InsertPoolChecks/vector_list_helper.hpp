/**
 * A simple template helper to create Function Type Arguments
 *
 * $Id: vector_list_helper.hpp,v 1.2 2008-08-27 21:37:05 criswell Exp $
 **/

#ifndef _VECTOR_LIST_HELPER_HPP_
#define _VECTOR_LIST_HELPER_HPP_

#include <vector>

namespace llvm
{
struct Type;
template <class T>
struct args {
	typedef T t_arg;
	typedef std::vector<t_arg> t_list;
	static t_list list(t_arg ty1) {
		const t_arg arr[] = {ty1};
		return t_list(arr, arr + sizeof(arr) / sizeof(t_arg)); 
	};
	static t_list list(t_arg ty1, t_arg ty2) {
		const t_arg arr[] = {ty1, ty2};
		return t_list(arr, arr + sizeof(arr) / sizeof(t_arg)); 
	};
	static t_list list(t_arg ty1, t_arg ty2, t_arg ty3) {
		const t_arg arr[] = {ty1, ty2, ty3};
		return t_list(arr, arr + sizeof(arr) / sizeof(t_arg)); 
	};
	private:
		args();
};

}
#endif
