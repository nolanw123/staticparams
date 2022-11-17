//
// some experiments with template arguments as parameters
//
// with -O3 and --std=c++20 on godbolt.org we're able to get a single mov instruction as the result,
// which is a pretty strong hint the compiler was able to see through our code and evaluate it
// at compile time
//
// handy note:
//
// we can get an annotated dump like this:
// objdump -d -M intel -S objs/opt/test.o  > test.asm
// 

#include "static_types.h"

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

int main(int argc, char **argv)
{  
  calc<0.9999, 0.998, 0.9333, 0.5> instance;

  double val = instance.update();

  calc2<tlist<double, 0.5,0.25>, tlist<uint64_t,1,2>> instance2;

  calc3<tstrlist<tstr("chicken"), tstr("beef")>,
	  tmap<std::string_view, std::string_view,
	       std::pair<tstr("chicken"), tstrlist<tstr("foo"), tstr("bar")>>,
	       std::pair<tstr("beef"), tstrlist<tstr("baz"), tstr("bat")>>>,
	  tlist<double, 0.5, 0.25>> instance3;

  val += instance2.update();
  val += instance3.update();

  thlist<calc<0.9999, 0.998, 0.9333, 0.5>, calc2<tlist<double, 0.5,0.25>, tlist<uint64_t,1,2>>> slist;

  double lval = 0.0;
  slist.visit([&lval](auto &v) { lval += v.update(); }); // this actually compiles to nothing but we get lval updated!
  
  return val + lval; // should return 12
}
