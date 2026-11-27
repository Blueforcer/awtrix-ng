#include "../support.h"

#include "platform/linux/hostd/CodeAccess.h"

using awtrix::hostd::CodeRequest;
using awtrix::hostd::showsCode;
using awtrix::test::check;

namespace {

CodeRequest local() {
  CodeRequest r;
  r.peer = "127.0.0.1";
  r.host = "127.0.0.1:8300";
  r.hostCount = 1;
  return r;
}

CodeRequest ingress() {
  CodeRequest r;
  r.peer = "172.30.32.2";
  r.host = "homeassistant.local:8123";
  r.hostCount = 1;
  r.ingressPath = "/api/hassio_ingress/abc";
  r.ingressPathCount = 1;
  r.fetchSite = "same-origin";
  r.fetchSiteCount = 1;
  return r;
}

}

int main() {
  check(showsCode(local(), 8300, false), "a tool on this computer sees the code");
  CodeRequest named = local();
  named.host = "localhost:8300";
  check(showsCode(named, 8300, true), "so does a browser at localhost, with or without Home Assistant");
  CodeRequest rebound = local();
  rebound.host = "evil.example:8300";
  check(!showsCode(rebound, 8300, true), "a rebound name never sees it");
  CodeRequest otherPort = local();
  otherPort.host = "127.0.0.1:8301";
  check(!showsCode(otherPort, 8300, false), "nor a Host of another port");
  CodeRequest foreign = local();
  foreign.origin = "http://evil.example";
  foreign.originCount = 1;
  check(!showsCode(foreign, 8300, false), "nor a page of another site");

  check(showsCode(ingress(), 8300, true), "Home Assistant's Open Web UI sees the code");
  check(!showsCode(ingress(), 8300, false), "but only when the host runs as a Home Assistant app");
  CodeRequest lan = ingress();
  lan.peer = "192.168.1.50";
  check(!showsCode(lan, 8300, true), "another computer never sees it, even with X-Ingress-Path");
  CodeRequest neighbour = ingress();
  neighbour.peer = "172.30.32.3";
  check(!showsCode(neighbour, 8300, true), "nor another app in Home Assistant's network");
  CodeRequest direct = ingress();
  direct.ingressPath.clear();
  direct.ingressPathCount = 0;
  check(!showsCode(direct, 8300, true), "nor a call from the Supervisor's address without X-Ingress-Path");
  CodeRequest twice = ingress();
  twice.ingressPathCount = 2;
  check(!showsCode(twice, 8300, true), "nor one with X-Ingress-Path twice");
  CodeRequest crossSite = ingress();
  crossSite.fetchSite = "cross-site";
  check(!showsCode(crossSite, 8300, true), "nor a page of another site through Home Assistant");
  CodeRequest posted = ingress();
  posted.origin = "https://homeassistant.local:8123";
  posted.originCount = 1;
  check(showsCode(posted, 8300, true), "an Origin of Home Assistant itself is fine");
  posted.origin = "https://evil.example";
  check(!showsCode(posted, 8300, true), "an Origin of another site is not");
  return awtrix::test::finish("hostd code access");
}
