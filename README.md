# Static Parameters With c++20

## Motivation

Imagine we have a use case where it's desirable to at one stage of development have dynamic behavior, possibly defined by config
(JSON, or the like).  We have a variety of classes that read config, build up some internal structures, and at runtime use those
values to determine their behavior.

For example, a class that contains a vector of floating point values to multiply some value by:

```
class calc {
public:
// ...
  double calc() {
    double total = 0.0;
    for(auto m : _multipliers) {
      total += m * _val;
    }
    return total;
  }

private:
// ...
  std::vector<double> _multipliers;
  double _val;
};
```

What if at some point in time we wanted to fix the values in _multipliers to be some static subset of the config?
The holy grail would be some way to help the compiler see that all the values are constant, and thus enable many
opportunities for it to optimize that it couldn't exploit before.

## c++20 To The Rescue

As it turns out, some recent language features from c++20 can help us accomplish this along with our good friend templates.  However, first we have to think about what sorts of parameters we might want to enable our classes to have compile-time access to.  Here's a basic list:

- Basic non-type parameters
- String literals
- Lists of parameters (homogeneous, heterogeneous)
- Maps

### Non-Type Parameters

c++20 now enables us to pass a wider variety of values as template arguments.  Previously, it was only possible to pass a handful of types:

- integral types
- various pointer types
- enumeration types

Which honestly was a bit unreasonable -- why not allow more complex types if they are simple enough?  Why not a floating point type?  With c++20 we now can have, among others:

- floating point type
- literal class type (as long as [structural](https://en.cppreference.com/w/cpp/language/template_parameters))

Using variadic templates, here's an example of a class that takes a list of doubles as template parameters:

```
template <double... COEFS>
class calc
{
public:
  double update() {
    double coefs[sizeof...(COEFS)] = {COEFS...};
    double sum = 0;
    for(size_t i = 0 ; i < sizeof...(COEFS) ; ++i) {
      sum += coefs[i];
    }
    return sum;
  }
  
private:
  double _values[sizeof...(COEFS)];
};
```

A few things to note here:

- We haven't used constexpr anywhere yet, but the compiler will identify the coefs local array as constant, since we use the parameter pack expansion in its initializer list
- Likewise, the for loop and the member array size specification use the sizeof... construct to ensure they are also known at compile time as constants

This is all great, but we are now restricted to just one list of arguments, all of the same type.  Surely we can do better?  Yes, we can -- by wrapping parameters in another type.

### String Literals

Another frustration many have encountered is the inability to use string literals as template parameters at compile time.  With c++17 we got access to std::string_view, which wraps string literals.  Bolstered by c++20's increased power of constexpr, we can in fact wrap a string literal as follows:

```
// we have to use a macro for our tstr "type"
#define tstr(s) decltype([](){ return std::string_view(s); })
```

The real magic here is that c++20 allows lambdas as template parameters in an unevaluated context.  Which is all rather mind-blowing, because at the end, like many of the new constexpr features it really shows that, when used, the compiler is actually running some of your code at compile-time.

### Lists Of Parameters

Using variadic templates, we can wrap the concept of a homogeneous list of values of a particular type:

```
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
```

Again, we use variadic templates and initializer lists, and this time add constexpr to specify that all the work should be done at compile time where possible.

What if we want a heterogenous list?  This seems impossible, and it is to some extent -- for example, `operator[]` wouldn't be able to return more than one type.  We can turn the problem on its head a bit, and use the visitor pattern to achieve our goals:

```
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
```

We use `std::tuple` to "store" the arguments, and provide two "visit" functions templated on a functor (one which takes an index, and one which applies the functor across all items).  Note the use of the `requires` clause to enforce limits at compile time.  We use `constexpr` to simplify the recursion, as well.

Here's an example of usage of what we've built so far, assuming two classes `calc` and `calc2` both of which have an `update` member returning a floating point value.  The `calc` parameter is as described previously.  `calc2` is defined like this:

```
template <typename COEFS, typename IDS>
class calc2 {
public:

  double update() {
    double sum = 0;
    size_t ctr = 0;
    for(size_t i = 0 ; i < _coefs.size() ; ++i) {
      for(size_t j = 0 ; j < _ids.size() ; ++j) {      
	sum += _coefs[i] * _ids[j];
	_values[ctr++] = sum;
      }
    }
    return _values[ctr-1]; 
  }
  
private:
  COEFS _coefs;
  IDS _ids;
    
  double _values[COEFS::size() * IDS::size()];
};
```

Note how both the COEFS and IDS template parameters, when stored as members allow the values to be used in the same way as std::vector -- except everything happens at compile time.

Now we can instantiate a heterogeneous list of `calc` and `calc2` with fixed values, and visit each with a lambda:

```
  thlist<calc<0.9999, 0.998, 0.9333, 0.5>, calc2<tlist<double, 0.5,0.25>, tlist<uint64_t,1,2>>> slist;

  double lval = 0.0;
  slist.visit([&lval](auto &v) { lval += v.update(); }); // this actually compiles to nothing but we get lval updated!
```

The compiler evaluates everything at compile time.

### Maps

The real challenge will be an unordered map type.  If we require that the key and value types are specified up front, and that all the map contents are specified as `std::pair`'s of those types, we can in fact accomplish everything at compile time with variadic template recursion.  One wrinkle: we need to special case maps of keys to lists (again, because ech list element is a unique type, we can't return more than one type).  We'll do this by providing a special lookup operator for the n'th element in a list with a particular key.  We also provide a way to get the length of said list, which lets us iterate over it in our code:

```
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
```

This might look a little overwhelming, but an example with a new class `class3` should help. Here's `class3`'s definition:

```
template <typename GROUPS, typename GROUPDEFS, typename GROUPCOEFS>
class calc3 {
public:

  constexpr double update() {
    double sum = 0;
    size_t ctr = 0;
    for(size_t i = 0 ; i < _groups.size() ; ++i) {
      size_t tctr = 0;            
      std::string_view group = _groups[i];
      // lookup all names in group
      size_t groupsize = _groupdefs.size(group);
      for(size_t ni = 0 ; ni < groupsize; ++ni) {
	if(_groupdefs(group, ni) == "baz") {
	  ++tctr;
	}
      }
      for(size_t j = 0 ; j < _coefs.size() ; ++j) {	
	sum += tctr * _coefs[j];
	_values[ctr++] = sum;	
      }
    }
    return _values[ctr-1]; 
  }
  
private:
  GROUPS _groups;
  GROUPDEFS _groupdefs;
  GROUPCOEFS _coefs;
    
  double _values[GROUPS::size() * GROUPCOEFS::size()];
};
```

`class3` takes a list of map keys (`GROUPS`).  It iterates over the keys to look them up in the map `GROUPDEFS`, which is a map from `tstr` to a `tstrlist` (we have to special case lists of tstr's because we can't lock the list value type up front like we can in `tlist`).  When we find a particular element in the list, we run our logic.  Lastly, we have a lst of doubles defined by `GROUPCOEFS` that also are used.

Here's what the template instantiation of this would look like:

```
  calc3<tstrlist<tstr("chicken"), tstr("beef")>,
	  tmap<std::string_view, std::string_view,
	       std::pair<tstr("chicken"), tstrlist<tstr("foo"), tstr("bar")>>,
	       std::pair<tstr("beef"), tstrlist<tstr("baz"), tstr("bat")>>>,
	  tlist<double, 0.5, 0.25>> instance3;
```

Again, any call to `instance3`'s update compiles down to a constant value.

## Testing It Out

The ultimate test of this is to try compiling (optimized, of course) an example to verify that the compiler is actually able to evaluate everything at compile time.  `test.cc` can be made, and will return the result of the calculation as follows -- we expect to get the value 12:

```
nolanw@Adrenalin ~/staticparams $ make
g++ -DDEBUG_BUILD -g -MD -fPIC --std=c++20 -Wall -I .  -c test.cc -o objs/debug/test.o
g++ objs/debug/test.o -g      -o exec/debug/test
g++ -DRELEASE_BUILD -g -O3 -MD -fPIC --std=c++20 -Wall -I .  -c test.cc -o objs/opt/test.o
g++ objs/opt/test.o    -o exec/opt/test
nolanw@Adrenalin ~/staticparams $ ./exec/opt/test
nolanw@Adrenalin ~/staticparams $ echo $?
12
```

To be 100% sure we got a constant result in the code generated, we can use `objdump`:

```
nolanw@Adrenalin ~/staticparams $ objdump -d -M intel -S objs/opt/test.o  > test.asm
nolanw@Adrenalin ~/staticparams $ cat test.asm

objs/opt/test.o:     file format elf64-x86-64


Disassembly of section .text.startup:

0000000000000000 <main>:

  double _values[GROUPS::size() * GROUPCOEFS::size()];
  };

int main(int argc, char **argv)
{
   0:   f3 0f 1e fa             endbr64

  double lval = 0.0;
  slist.visit([&lval](auto &v) { lval += v.update(); }); // this actually compiles to nothing but we get lval updated!

  return val + lval; // should return 12
}
   4:   b8 0c 00 00 00          mov    eax,0xc
   9:   c3                      ret
```

Ignoring the annotation artifacts, there we have it: `mov eax,0xc` followed by a `ret`.

## Conclusion

In reality, there may never be an opportunity to completely evaluate everything at compile time.  What we hope to provide
is opportunities for the compiler to optimize beyond what it could if it were presented with logic based on traversing dynamic structures
at run time.

Perhaps a more practical approach to integrating this style of programming would be to use code generation.  We can still retain our config-driven approach: the code generator can parse JSON config to generate instances of our classes with the parameters set.  At a later stage, we can reduce down the set of parameters to ones that would actually be used in the release build of our code.

Another (as-of-yet unexplored) approach, while more complex, could be more flexible: somewhere use an `ifdef` to switch our behavior from getting config from config files (and our storage of the values to `std::vector`, `std::unordered_map`, etc) to our static parameter list behavior.  This would remove the need for a code-generation stage that needs to be run during make every time the config is changed -- which could be time-consuming during the pre-release phase of development.