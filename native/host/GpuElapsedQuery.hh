#pragma once
#ifndef __APPLE__
#include <epoxy/gl.h>
#endif
namespace bts {
class GpuElapsedQuery {
#ifndef __APPLE__
 GLuint query_=0;
#endif
 public:
 ~GpuElapsedQuery(){
#ifndef __APPLE__
 if(query_)glDeleteQueries(1,&query_);
#endif
 }
 void begin(){
#ifndef __APPLE__
 glGenQueries(1,&query_);glBeginQuery(GL_TIME_ELAPSED,query_);
#endif
 }
 void end(){
#ifndef __APPLE__
 glEndQuery(GL_TIME_ELAPSED);
#endif
 }
 double milliseconds(){
#ifndef __APPLE__
 GLuint64 elapsed=0;glGetQueryObjectui64v(query_,GL_QUERY_RESULT,&elapsed);return double(elapsed)/1e6;
#else
 return -1; // Unsupported, not a synthetic zero-duration GPU measurement.
#endif
 }
};
}
