#ifndef HTTP_STATIC_CONTENT_H_
#define HTTP_STATIC_CONTENT_H_

#include <string>

#include "base/types.h"
#include "http/http-server.h"

namespace sling {

// HTTP handler for serving static web content.
class StaticContent {
 public:
  // Initialize handler for serving files from a directory.
  StaticContent(const string &url, const string &path);

  // Register handler with HTTP server.
  void Register(HTTPServer *http);

  // Serve static web content from directory.
  void HandleFile(HTTPRequest *request, HTTPResponse *response);

 private:
  // URL path for static content.
  string url_;

  // Directory with static web content to be served.
  string dir_;
};

}  // namespace sling

#endif  // HTTP_STATIC_CONTENT_H_
