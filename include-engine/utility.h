#ifndef UTILITY_H
#define UTILITY_H

[[noreturn]] void fail_fast();

template<class T> struct narrower
{
    const T & value;
    operator T () const { return value; }
    template<class U> operator U () const
    {
        U narrowed_value = static_cast<U>(value);
        if(value != narrowed_value) fail_fast();
        return narrowed_value;
    }
};
template<class T> narrower<T> narrow(const T & value) 
{ 
    return {value}; 
}

#endif
