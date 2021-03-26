#include "td/net/DarwinHttp.h"

#import <Foundation/Foundation.h>

namespace td {
namespace {
NSString *to_ns_string(CSlice slice) {
  return [NSString stringWithUTF8String:slice.c_str()];
}

NSData *to_ns_data(Slice data) {
  return [NSData dataWithBytes:static_cast<const void*>(data.data()) length:data.size()];
}

auto http_get(CSlice url) {
  auto nsurl = [NSURL URLWithString:to_ns_string(url)];
  auto request = [NSURLRequest requestWithURL:nsurl];
  return request;
}

auto http_post(CSlice url, Slice data) {
  auto nsurl = [NSURL URLWithString:to_ns_string(url)];
  auto request = [NSMutableURLRequest requestWithURL:nsurl];
  [request setHTTPMethod:@"POST"];
  [request setHTTPBody:to_ns_data(data)];
  [request setValue:@"keep-alive" forHTTPHeaderField:@"Connection"];
  [request setValue:@"" forHTTPHeaderField:@"Host"];
  [request setValue:to_ns_string(PSLICE() << data.size()) forHTTPHeaderField:@"Content-Length"];
  [request setValue:@"application/x-www-form-urlencoded" forHTTPHeaderField:@"Content-Type"];
  return request;
}

void http_send(NSURLRequest *request, Promise<BufferSlice> promise) { 
  __block auto callback = std::move(promise);
  NSURLSessionDataTask* dataTask = 
    [NSURLSession.sharedSession
      dataTaskWithRequest:request
      completionHandler:
        ^(NSData *data, NSURLResponse *response, NSError *error) {
          if(error == nil) {
             callback(BufferSlice(Slice((const char *)([data bytes]), [data length])));
          } else {
             callback(Status::Error(static_cast<int32>([error code])));
          }
        }];
  [dataTask resume];
}
}

void DarwinHttp::get(CSlice url, Promise<BufferSlice> promise) {
  return http_send(http_get(url), std::move(promise));
}

void DarwinHttp::post(CSlice url, Slice data, Promise<BufferSlice> promise) {
  return http_send(http_post(url, data), std::move(promise));
}
}
