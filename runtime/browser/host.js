/* cerco browser host — the only JavaScript in the framework.
 *
 * Responsibilities:
 *  - node registry + DOM command batch processing
 *  - event delegation/registration into wasm
 *  - history API + link interception + popstate
 *  - fetch plumbing with bounded response buffers
 *  - hydration handshake (scan data-cerco roots, pass props, call mounts)
 *
 * No framework, no virtual DOM, no dependencies.
 */
(function () {
  'use strict';

  var decoder = new TextDecoder('utf-8');
  var encoder = new TextEncoder();

  var wasm = null;         // WebAssembly.Instance exports
  var memory = null;
  var registry = [null, null]; // node ids: 1 = document.body (2 = head unused)
  var roots = [];          // hydration root ids (host-assigned)
  var rootMeta = [];       // {name, props} per root
  var nextNodeId = 10;

  function reg(id) { return registry[id] || null; }

  function getStr(ptr, len) {
    return decoder.decode(new Uint8Array(memory.buffer, ptr, len));
  }

  /* ------------------------------------------------ DOM command processing */

  var handlers = new Map(); // node id -> {type -> [slot]}

  function processCommand(view, bytes, pos) {
    var op = view.getUint8(pos); pos += 1;
    function u32() { var v = view.getUint32(pos, true); pos += 4; return v; }
    function str() {
      var len = view.getUint32(pos, true); pos += 4;
      var s = decoder.decode(bytes.subarray(pos, pos + len));
      pos += len;
      return s;
    }
    switch (op) {
      case 1: { // CREATE: u32 id, str tag, u32 parent
        var id = u32(), tag = str(), parent = u32();
        var el = document.createElement(tag);
        registry[id] = el;
        var p = reg(parent);
        if (p) p.appendChild(el);
        break;
      }
      case 2: { var id = u32(), t = str(); var el = reg(id); if (el) el.textContent = t; break; }
      case 3: { var id = u32(), k = str(), v = str(); var el = reg(id); if (el) el.setAttribute(k, v); break; }
      case 4: { var id = u32(), k = str(); var el = reg(id); if (el) el.removeAttribute(k); break; }
      case 5: { var parent = u32(), child = u32(); var p = reg(parent), c = reg(child); if (p && c) p.appendChild(c); break; }
      case 6: { var id = u32(); var el = reg(id); if (el && el.parentNode) el.parentNode.removeChild(el); delete registry[id]; break; }
      case 7: { var id = u32(), c = str(); var el = reg(id); if (el) el.className = c; break; }
      case 8: { var id = u32(), c = str(); var el = reg(id); if (el) el.classList.add(c); break; }
      case 9: { var id = u32(), c = str(); var el = reg(id); if (el) el.classList.remove(c); break; }
      case 10: { // ADD_EVENT: u32 node, str type, u32 slot
        var id = u32(), type = str(), slot = u32();
        var el = reg(id);
        if (!el) break;
        var h = handlers.get(id);
        if (!h) { h = {}; handlers.set(id, h); }
        if (!h[type]) {
          h[type] = [];
          el.addEventListener(type, function (ev) {
            var slots = (handlers.get(id) || {})[type];
            if (!slots) return;
            for (var i = 0; i < slots.length; i++) {
              try { wasm.exports.cerco_event(id, slots[i]); }
              catch (e) { console.error('cerco event error', e); }
            }
          });
        }
        h[type].push(slot);
        break;
      }
      case 11: { var id = u32(), v = str(); var el = reg(id); if (el && 'value' in el) el.value = v; break; }
      case 12: { // SET_INNER_HTML: only handlers inside the replaced subtree die
        var id = u32(), html = str();
        var el = reg(id);
        if (el) {
          el.innerHTML = html;
          handlers.forEach(function (h, nid) {
            var n = registry[nid];
            if (n && n !== el && el.contains(n)) handlers.delete(nid);
          });
        }
        break;
      }
      default: break; // unknown op: skip is impossible (lengths unknown) -> must keep ops in sync
    }
    return pos;
  }

  function domFlush(ptr, len) {
    var view = new DataView(memory.buffer, ptr, len);
    var bytes = new Uint8Array(memory.buffer, ptr, len);
    var pos = 0;
    while (pos < len) {
      var before = pos;
      pos = processCommand(view, bytes, pos);
      if (pos <= before) break; // safety
    }
  }

  /* ------------------------------------------------------------- imports */

  var imports = {
    cerco: {
      dom_flush: function (ptr, len) { domFlush(ptr, len); },
      query: function (scope, ptr, len) {
        var el = reg(scope) || document;
        var sel = getStr(ptr, len);
        var found = el.querySelector(sel);
        if (!found) return 0;
        var id = nextNodeId++;
        registry[id] = found;
        return id;
      },
      value: function (node, outPtr, cap) {
        var el = reg(node);
        if (!el || !('value' in el)) return -1;
        var v = encoder.encode(el.value);
        if (v.length > cap) return -1;
        new Uint8Array(memory.buffer, outPtr, v.length).set(v);
        return v.length;
      },
      attr: function (node, kPtr, kLen, outPtr, cap) {
        var el = reg(node);
        if (!el) return -1;
        var v = el.getAttribute(getStr(kPtr, kLen));
        if (v === null) return -1;
        var b = encoder.encode(v);
        if (b.length > cap) return -1;
        new Uint8Array(memory.buffer, outPtr, b.length).set(b);
        return b.length;
      },
      fetch: function (id, mPtr, mLen, uPtr, uLen, bPtr, bLen) {
        var method = getStr(mPtr, mLen);
        var url = getStr(uPtr, uLen);
        var body = bLen > 0 ? new Uint8Array(memory.buffer, bPtr, bLen).slice() : null;
        fetch(url, {
          method: method,
          body: body,
          headers: body ? { 'Content-Type': 'application/x-cerco-sf' } : undefined
        }).then(function (resp) {
          return resp.arrayBuffer().then(function (buf) {
            var bytes = new Uint8Array(buf);
            // ask the client for a buffer sized to this body; 0 means the
            // body is over its cap, reported as -1 (never truncated)
            var ptr = wasm.exports.cerco_fetch_reserve(id, bytes.length);
            if (!ptr) { wasm.exports.cerco_fetch_done(id, -1, 0); return; }
            // read memory.buffer AFTER the reserve call: growing the wasm
            // heap detaches any ArrayBuffer taken before it
            new Uint8Array(memory.buffer, ptr, bytes.length).set(bytes);
            wasm.exports.cerco_fetch_done(id, resp.status, bytes.length);
          });
        }).catch(function () {
          wasm.exports.cerco_fetch_done(id, 0, 0);
        });
      },
      nav_push: function (ptr, len) {
        var path = getStr(ptr, len);
        history.pushState({ cerco: true }, '', path);
      },
      set_title: function (ptr, len) { document.title = getStr(ptr, len); },
      log: function (ptr, len) { console.log('[cerco]', getStr(ptr, len)); },
      location: function (ptr, cap) {
        var p = encoder.encode(location.pathname + location.search);
        var n = Math.min(p.length, cap);
        new Uint8Array(memory.buffer, ptr, n).set(p.subarray(0, n));
        return n;
      },
      root_id: function (i) { return roots[i] || 0; },
      hydrate_roots: function () {
        // scan interactive roots, assign ids, then hand each to wasm
        var els = document.querySelectorAll('[data-cerco]');
        roots = [];
        rootMeta = [];
        els.forEach(function (el) {
          var id = nextNodeId++;
          registry[id] = el;
          roots.push(id);
          rootMeta.push({
            name: el.getAttribute('data-cerco') || '',
            props: el.getAttribute('data-cerco-props') || ''
          });
        });
        for (var i = 0; i < roots.length; i++) hydrateRoot(i);
        return roots.length;
      },
      nav_reload: function () { location.reload(); },
      swap_page: function (ptr, len) {
        // replace only the nodes between the cerco:page markers; the layout
        // (header/nav/footer) lives outside them and survives the swap.
        // returns 1 on success, 0 when the markers are missing.
        var html = getStr(ptr, len);
        var start = null, end = null;
        var walker = document.createTreeWalker(
          document.body, NodeFilter.SHOW_COMMENT, null);
        var node;
        while ((node = walker.nextNode())) {
          var v = node.nodeValue;
          if (v === 'cerco:page') start = node;
          else if (v === 'cerco:/page') { end = node; break; }
        }
        if (!start || !end) return 0;
        var n = start.nextSibling;
        while (n && n !== end) {
          var next = n.nextSibling;
          n.parentNode.removeChild(n);
          n = next;
        }
        var tpl = document.createElement('template');
        tpl.innerHTML = html;
        end.parentNode.insertBefore(tpl.content, end);
        sweepDetached();
        return 1;
      }
    }
  };

  /* --------------------------------------------------------- hydration */

  function hydrateRoot(i) {
    var meta = rootMeta[i];
    if (!meta) return;
    // getAttribute already returns entity-decoded values (server escapes them)
    var json = meta.props;
    var nameBytes = encoder.encode(meta.name);
    var propsBytes = encoder.encode(json);
    var scratch = wasm.exports.cerco_scratch();
    var cap = wasm.exports.cerco_scratch_size();
    var need = 4 + nameBytes.length + 4 + propsBytes.length;
    if (need > cap) return;
    var view = new DataView(memory.buffer, scratch, need);
    var pos = 0;
    function putU32(v) { view.setUint32(pos, v, true); pos += 4; }
    putU32(nameBytes.length);
    new Uint8Array(memory.buffer, scratch + pos, nameBytes.length).set(nameBytes);
    pos += nameBytes.length;
    putU32(propsBytes.length);
    new Uint8Array(memory.buffer, scratch + pos, propsBytes.length).set(propsBytes);
    pos += propsBytes.length;
    wasm.exports.cerco_hydrate_root(i);
  }

  /* forget handlers + registry entries whose nodes left the document */
  function sweepDetached() {
    handlers.forEach(function (h, id) {
      var el = registry[id];
      if (el && id > 1 && !el.isConnected) handlers.delete(id);
    });
    for (var i = 2; i < registry.length; i++) {
      if (registry[i] && !registry[i].isConnected) delete registry[i];
    }
  }

  /* ------------------------------------------------- navigation interception */

  document.addEventListener('click', function (ev) {
    if (ev.defaultPrevented || ev.button !== 0 || ev.metaKey || ev.ctrlKey ||
        ev.shiftKey || ev.altKey) return;
    var a = ev.target.closest && ev.target.closest('a');
    if (!a) return;
    var href = a.getAttribute('href');
    if (!href || !href.startsWith('/') || a.target === '_blank' ||
        a.hasAttribute('download') || a.hasAttribute('data-cerco-external')) return;
    ev.preventDefault();
    history.pushState({ cerco: true }, '', href);
    navigateTo(href);
  });

  window.addEventListener('popstate', function () {
    navigateTo(location.pathname + location.search);
  });

  function navigateTo(path) {
    var p = encoder.encode(path);
    var scratch = wasm.exports.cerco_scratch();
    new Uint8Array(memory.buffer, scratch, p.length).set(p);
    wasm.exports.cerco_navigate(p.length);
  }

  /* ---------------------------------------------------------------- boot */

  function boot() {
    if (!window.WebAssembly) return; // progressive enhancement: SSR still works
    fetch('/assets/app.wasm').then(function (r) { return r.arrayBuffer(); })
      .then(function (buf) {
        return WebAssembly.instantiate(buf, imports);
    }).then(function (result) {
      wasm = result.instance;
      memory = wasm.exports.memory;
      registry[1] = document.body;
      window.__cerco = { wasm: wasm, registry: registry, roots: function () { return roots; } };
      wasm.exports.cerco_boot();
    }).catch(function (e) {
        console.error('cerco: wasm boot failed', e);
      });
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', boot);
  } else {
    boot();
  }
})();
