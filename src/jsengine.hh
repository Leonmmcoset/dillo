/* QuickJS/HTML5 document engine. No GUI or networking dependencies. */
#ifndef DILLO_JSENGINE_HH
#define DILLO_JSENGINE_HH

#include <string>
#include <vector>
#include <stddef.h>

class JsEngine {
public:
   class Host {
   public:
      virtual ~Host() {}
      virtual void log(const std::string &message) = 0;
      virtual void changed(int delayMilliseconds = 0) = 0;
      virtual void focusChanged(int node, bool focused) { (void)node; (void)focused; }
      virtual void title(const std::string &text) = 0;
      virtual void navigate(const std::string &url, bool replace) = 0;
      virtual void timer(int id, int milliseconds) = 0;
      virtual void request(int id, const std::string &url,
                           const std::string &method, const std::string &body,
                           const std::string &headers, int credentials) = 0;
      virtual void abort(int id) = 0;
      virtual std::string userAgent() = 0;
      virtual std::string control(int node, const std::string &property,
                                  const std::string *value) = 0;
   };
   struct Script {
      int node;
      std::string source, url;
      bool async, defer, parserInserted;
   };
   struct MouseEvent {
      int clientX = 0, clientY = 0, pageX = 0, pageY = 0, screenX = 0, screenY = 0;
      int button = 0, buttons = 0, detail = 0;
      bool ctrlKey = false, shiftKey = false, altKey = false, metaKey = false;
   };

private:
   struct Impl;
   Impl *impl;
   JsEngine(const JsEngine &);
   JsEngine &operator=(const JsEngine &);

public:
   JsEngine(Host *host, const std::string &url);
   ~JsEngine();
   bool valid() const;
   bool evaluate(const std::string &source, const std::string &filename);
   void append(const char *data, size_t size);
   void finish();
   bool nextScript(Script &script);
   void scriptStarted(int node);
   bool parsed() const;
   bool hasPendingJobs() const;
   void interactive();
   void loaded();
   void timeout(int id);
   void response(int id, int status, const std::string &body,
                 const std::string &headers, const std::string &url);
   bool event(int node, const std::string &type, const std::string &key = "", int keyCode = 0,
               const MouseEvent *mouse = NULL);
   std::string serialize(bool rendering = false);
   std::string stringValue(const std::string &expression);
   int find(const std::string &selector);
   void setAttribute(int node, const std::string &name, const std::string &value);
   void setControlState(int node, const std::string &name, const std::string &value);
   void setUrl(const std::string &url);
   void historyTraversal(const std::string &oldUrl);
   void stop();
};
#endif
