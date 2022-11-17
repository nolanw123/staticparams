#ifndef __STATIC_TYPES_H__
#define __STATIC_TYPES_H__

#include <tuple>

#include <stddef.h>
#include <stdint.h>
#include <stdexcept>

// this template struct can store a variadic list of arguments of a type T
// (for example tlist<int64_t, 5, 7, -3>)
template<typename T, T... ARGS>
struct tlist
{
  static constexpr size_t size() { return sizeof...(ARGS); }
  typedef T value_type;
  constexpr T operator[](size_t i_) const { return values[i_]; }

  T values[sizeof...(ARGS)] = {ARGS...};  
};

// this template struct can store a heterogenous list
// and invoke a visitor across all of them, or just one of them
// -- this can potentially be all done at compile time
template <typename... ARGS>
struct thlist {
  static constexpr size_t size() { return sizeof...(ARGS); }
  
  std::tuple<ARGS...> items = {ARGS()...};
  
  template <size_t N, typename VISITOR> requires (N < sizeof...(ARGS))
    constexpr void visit(VISITOR visitor) {
    visitor(std::get<N>(items));    
    if constexpr (N) {
      visit<N-1>(visitor);
    }
  }

  template <typename VISITOR> requires (sizeof...(ARGS) > 0)
    constexpr void visit(VISITOR visitor) {
    visit<sizeof...(ARGS) - 1>(visitor);
  }

  template <size_t N, typename VISITOR> requires (N < sizeof...(ARGS))
    constexpr void ivisit(VISITOR visitor, size_t i) {
    if(i == N) {
      visitor(std::get<N>(items));
    }
    if constexpr (N) {
      ivisit<N-1>(visitor, i);
    }
    throw std::out_of_range("out of range in ivisit");
  }

  template <typename VISITOR> requires (sizeof...(ARGS) > 0)
  constexpr void visit(VISITOR visitor, size_t i) {
    ivisit<sizeof...(ARGS) - 1>(visitor, i);
  }
  
};

// this template struct can store a compile-time initialized mapping from KEYs to VALUEs
// note that since linear search is used, it's not recommended to make huge maps
//
// in the case of a map from KEY to values that are lists (i.e. tlist or tstrlist)
// we need to access them differently because they are not heterogeneous --
// so, we can provide a size(key) method that gets the length of the list at key
// and a get(key, i) that gets the ith element in the list by key "key"
// -- in this use case VALUE is the type of the element in the list
//
// map of lists example:
// ---------------------
// tmap<std::string, std::pair<tstr("key1"), tstrlist<tstr("val1_1"), tstr("val1_2")>>,
//                   std::pair<tstr("key2"), tstrlist<tstr(val2_1"), tstr("val2_2")>>> foo;
// for(const auto &key : foo.keys()) {
//   size_t len = foo.size(key);
//   std::cout << key << " :";
//   for(size_t i = 0 ; i < len ; ++i) {
//      std::cout << " " << foo.get(key, i)
//   }
//   std::cout << std::endl;
//
template<typename KEY, typename VALUE, typename... KVPAIRS>
struct tmap
{
  typedef KEY key_type;
  typedef VALUE value_type;

  // return number of keys in map
  static constexpr size_t size() { return sizeof...(KVPAIRS); }
  
  template <size_t N, typename KV, typename... REST> requires (N < sizeof...(KVPAIRS))
    static constexpr size_t size_helper(const KEY &key_)
  {
    if (KV().first() == key_) {
      return KV().second.size();
    }
    if constexpr (N) {
      return size_helper<N-1, REST...>(key_);
    }
    // oops, didn't find
    throw std::out_of_range("couldn't find key in tmap in size_helper");
  }

  // get the number of values in a tlist associated with key_
  static constexpr size_t size(const KEY& key_) { return size_helper<sizeof...(KVPAIRS) - 1, KVPAIRS...>(key_); }
  
  template <size_t N, typename KV, typename... REST> requires (N < sizeof...(KVPAIRS))
    constexpr VALUE index_helper(const KEY &key_) const
  {
    if (KV().first() == key_) {
      return KV().second;
    }
    if constexpr (N) {
      return index_helper<N-1, REST...>(key_);
    } else { // oops, didn't find
      throw std::out_of_range("couldn't find key in tmap in index_helper");
    }
  }

  // get the value associated with key_
  constexpr VALUE operator[](const KEY& key_) const
  {
    return index_helper<sizeof...(KVPAIRS) - 1, VALUE, KVPAIRS...>(key_);
  }

  template <size_t N, typename KV, typename... REST> requires (N < sizeof...(KVPAIRS))
    constexpr VALUE get_helper(const KEY &key_, size_t i_) const
  {
    if (KV().first() == key_) {
      return KV().second[i_];
    }
    if constexpr (N) {
	return get_helper<N-1, REST...>(key_, i_);
    } else { // oops, didn't find
      throw std::out_of_range("couldn't find key in tmap in get_helper");
    }
  }

  // get the ith element in a list that key_ refers to (obviously only works for tlists)
  // note: with high enough version of gcc + c++23 can do [,] notation instead  
  constexpr VALUE operator()(const KEY &key_, size_t i_) const  
  {
    return get_helper<sizeof...(KVPAIRS) - 1, KVPAIRS...>(key_, i_);
  }  
};

// we have to use a macro for our tstr "type"
#define tstr(s) decltype([](){ return std::string_view(s); })

// a variable-sized list of tstr's
template <typename... ARGS>
struct tstrlist
{
  static constexpr size_t size() { return sizeof...(ARGS); }
  
  template <size_t N, typename T, typename... REST> requires (N < sizeof...(ARGS))
    constexpr std::string_view get_helper(size_t i) const
  {
    if (i == N) {
      return T()();
    }
    if constexpr (N) {
      return get_helper<N-1, REST...>(i);
    }
    // oops, can't find
    throw std::out_of_range("index error in get_helper");
  }
  
  constexpr std::string_view operator[](size_t i) const
  {
    return get_helper<sizeof...(ARGS) - 1, ARGS...>(i);
  }  
};

#endif
