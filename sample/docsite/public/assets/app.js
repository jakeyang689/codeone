(function () {
  "use strict";

  const DOCS = [
    { id: "ssa", file: "ssa.md", title: "SSA 语言规范", desc: "类型、指令与语法约定" },
    { id: "box", file: "box.md", title: "标准库 Box", desc: "网络、TLS、加密与系统接口" },
    { id: "agents", file: "agents.md", title: "AI 编码规则", desc: "生成 SSA 的模式与反模式" },
    { id: "cmap", file: "cmap.md", title: "指令速查", desc: "伪 C 视角下的 SSA 结构" }
  ];

  const articleEl = document.getElementById("article");
  const listEl = document.getElementById("doc-list");
  const tocEl = document.getElementById("toc");
  const crumbEl = document.getElementById("crumb-doc");
  const topBtn = document.getElementById("to-top");
  let tocItems = [];
  let headingSeq = 0;

  function escapeHtml(s) {
    return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
  }

  function inline(text) {
    const codes = [];
    let out = escapeHtml(text);
    out = out.replace(/`([^`]+)`/g, function (_, code) {
      codes.push(code);
      return "\u0000" + (codes.length - 1) + "\u0000";
    });
    out = out.replace(/\[([^\]]+)\]\(([^)\s]+)\)/g, function (_, label, href) {
      const safe = /^(https?:|\/|#|mailto:)/i.test(href) ? href : "#";
      return '<a href="' + escapeHtml(safe) + '">' + label + "</a>";
    });
    out = out.replace(/\*\*([^*]+)\*\*/g, "<strong>$1</strong>");
    out = out.replace(/(^|[^*])\*([^*]+)\*(?!\*)/g, "$1<em>$2</em>");
    out = out.replace(/\u0000(\d+)\u0000/g, function (_, n) {
      return "<code>" + codes[Number(n)] + "</code>";
    });
    return out;
  }

  function slug(text) {
    return text
      .replace(/`([^`]*)`/g, "$1")
      .replace(/[*_~]/g, "")
      .trim()
      .toLowerCase()
      .replace(/[^\w一-龥]+/g, "-")
      .replace(/^-+|-+$/g, "") || "section";
  }

  function tableBlock(lines, start) {
    const split = function (line) {
      return line.replace(/^\s*\|/, "").replace(/\|\s*$/, "").split("|").map(function (c) { return c.trim(); });
    };
    const head = split(lines[start]);
    let i = start + 1;
    const sep = split(lines[i]);
    if (!sep.every(function (c) { return /^:?-{2,}:?$/.test(c) || c === ""; })) return null;
    i++;
    let html = '<table><thead><tr>';
    head.forEach(function (c) { html += "<th>" + inline(c) + "</th>"; });
    html += "</tr></thead><tbody>";
    while (i < lines.length && /^\s*\|/.test(lines[i])) {
      const cells = split(lines[i]);
      html += "<tr>";
      for (let c = 0; c < head.length; c++) html += "<td>" + inline(cells[c] || "") + "</td>";
      html += "</tr>";
      i++;
    }
    html += "</tbody></table>";
    return { html: html, next: i };
  }

  function listMatch(line) {
    const m = line.match(/^(\s*)([-*+]|\d+\.)\s+(.*)$/);
    if (!m) return null;
    return {
      indent: m[1].replace(/\t/g, "    ").length,
      ordered: /\d+\./.test(m[2]),
      text: m[3]
    };
  }

  function buildList(items, start, indent) {
    const ordered = items[start].ordered;
    let html = ordered ? "<ol>" : "<ul>";
    let i = start;
    while (i < items.length && items[i].indent >= indent) {
      if (items[i].indent > indent) {
        const sub = buildList(items, i, items[i].indent);
        html += sub.html;
        i = sub.next;
        continue;
      }
      let inner = inline(items[i].text);
      i++;
      if (i < items.length && items[i].indent > indent) {
        const sub = buildList(items, i, items[i].indent);
        inner += sub.html;
        i = sub.next;
      }
      html += "<li>" + inner + "</li>";
    }
    html += ordered ? "</ol>" : "</ul>";
    return { html: html, next: i };
  }

  function renderMarkdown(src) {
    const lines = src.replace(/\r\n?/g, "\n").split("\n");
    const out = [];
    let i = 0;
    headingSeq = 0;

    while (i < lines.length) {
      const line = lines[i];

      if (/^\s*$/.test(line)) { i++; continue; }

      const fence = line.match(/^\s*```\s*([\w+-]*)\s*$/);
      if (fence) {
        const lang = fence[1];
        const buf = [];
        i++;
        while (i < lines.length && !/^\s*```\s*$/.test(lines[i])) {
          buf.push(lines[i]);
          i++;
        }
        if (i < lines.length) i++;
        out.push(
          '<pre><button class="copy" type="button">复制</button><code' +
          (lang ? ' class="lang-' + escapeHtml(lang) + '"' : "") +
          ">" + escapeHtml(buf.join("\n")) + "</code></pre>"
        );
        continue;
      }

      const h = line.match(/^(#{1,6})\s+(.*)$/);
      if (h) {
        const level = h[1].length;
        const text = h[2].trim();
        const id = slug(text) + (headingSeq++ ? "-" + headingSeq : "");
        if (level === 2 || level === 3) tocItems.push({ level: level, text: text, id: id });
        out.push("<h" + level + ' id="' + id + '">' + inline(text) + "</h" + level + ">");
        i++;
        continue;
      }

      if (/^\s*\|/.test(line) && i + 1 < lines.length) {
        const t = tableBlock(lines, i);
        if (t) { out.push(t.html); i = t.next; continue; }
      }

      if (/^\s*([-*_])\s*\1\s*\1[\s\-*_]*$/.test(line)) {
        out.push("<hr>");
        i++;
        continue;
      }

      if (/^\s*>/.test(line)) {
        const buf = [];
        while (i < lines.length && /^\s*>/.test(lines[i])) {
          buf.push(lines[i].replace(/^\s*>\s?/, ""));
          i++;
        }
        out.push("<blockquote><p>" + inline(buf.join(" ")) + "</p></blockquote>");
        continue;
      }

      if (listMatch(line)) {
        const items = [];
        while (i < lines.length) {
          const m = listMatch(lines[i]);
          if (m) { items.push(m); i++; continue; }
          if (/^\s*$/.test(lines[i]) && i + 1 < lines.length && listMatch(lines[i + 1])) {
            i++;
            continue;
          }
          break;
        }
        if (items.length) {
          const res = buildList(items, 0, items[0].indent);
          out.push(res.html);
        }
        continue;
      }

      const para = [line.trim()];
      i++;
      while (i < lines.length && !/^\s*$/.test(lines[i]) &&
             !/^(#{1,6})\s+/.test(lines[i]) && !/^\s*```/.test(lines[i]) &&
             !/^\s*\|/.test(lines[i]) && !/^\s*>/.test(lines[i]) && !listMatch(lines[i])) {
        para.push(lines[i].trim());
        i++;
      }
      out.push("<p>" + inline(para.join(" ")) + "</p>");
    }

    return out.join("\n");
  }

  function buildSidebar(currentId) {
    listEl.innerHTML = "";
    DOCS.forEach(function (doc) {
      const a = document.createElement("a");
      a.href = "/docs#" + doc.id;
      a.dataset.id = doc.id;
      if (doc.id === currentId) a.className = "active";
      a.innerHTML = "<b>" + doc.title + "</b><span>" + doc.desc + "</span>";
      listEl.appendChild(a);
    });
  }

  function buildToc() {
    tocEl.innerHTML = "";
    if (!tocItems.length) {
      const p = document.createElement("p");
      p.className = "empty";
      p.textContent = "本页暂无小节";
      tocEl.appendChild(p);
      return;
    }
    tocItems.forEach(function (item) {
      const a = document.createElement("a");
      a.href = "#" + item.id;
      a.textContent = item.text;
      if (item.level === 3) a.className = "h3";
      tocEl.appendChild(a);
    });
  }

  function loadDoc(id) {
    const doc = DOCS.find(function (d) { return d.id === id; }) || DOCS[0];
    tocItems = [];
    buildSidebar(doc.id);
    crumbEl.textContent = doc.title;
    document.title = doc.title + " — codeone 文档";
    articleEl.innerHTML = '<div class="loading"><span class="spinner"></span>正在加载文档…</div>';

    fetch("/docs/" + doc.file, { cache: "no-cache" })
      .then(function (r) {
        if (!r.ok) throw new Error("HTTP " + r.status);
        return r.text();
      })
      .then(function (text) {
        articleEl.innerHTML = renderMarkdown(text);
        buildToc();
        if (location.hash && location.hash.length > 1) {
          const target = document.getElementById(location.hash.slice(1));
          if (target) requestAnimationFrame(function () { target.scrollIntoView(); });
        }
      })
      .catch(function (err) {
        articleEl.innerHTML = '<p class="err">文档加载失败：' + escapeHtml(err.message) + "</p>";
        buildToc();
      });
  }

  listEl.addEventListener("click", function (e) {
    const a = e.target.closest("a[data-id]");
    if (!a) return;
    e.preventDefault();
    const id = a.dataset.id;
    if (location.hash === "#" + id) return;
    history.pushState(null, "", "/docs#" + id);
    loadDoc(id);
    window.scrollTo({ top: 0, behavior: "smooth" });
  });

  document.addEventListener("click", function (e) {
    const btn = e.target.closest("pre .copy");
    if (!btn) return;
    const code = btn.parentElement.querySelector("code");
    const text = code ? code.textContent : "";
    const done = function () {
      btn.textContent = "已复制";
      setTimeout(function () { btn.textContent = "复制"; }, 1400);
    };
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).then(done, function () { fallbackCopy(text, done); });
    } else {
      fallbackCopy(text, done);
    }
  });

  function fallbackCopy(text, done) {
    const ta = document.createElement("textarea");
    ta.value = text;
    ta.style.position = "fixed";
    ta.style.opacity = "0";
    document.body.appendChild(ta);
    ta.select();
    try { document.execCommand("copy"); } catch (e) {}
    document.body.removeChild(ta);
    done();
  }

  window.addEventListener("scroll", function () {
    topBtn.classList.toggle("show", window.scrollY > 500);
  }, { passive: true });
  topBtn.addEventListener("click", function () {
    window.scrollTo({ top: 0, behavior: "smooth" });
  });

  window.addEventListener("hashchange", function () {
    const id = location.hash.slice(1);
    if (DOCS.some(function (d) { return d.id === id; })) {
      loadDoc(id);
      window.scrollTo({ top: 0, behavior: "smooth" });
    }
  });

  window.addEventListener("popstate", function () {
    const id = location.hash.slice(1);
    if (DOCS.some(function (d) { return d.id === id; })) loadDoc(id);
  });

  const initial = location.hash.slice(1);
  loadDoc(DOCS.some(function (d) { return d.id === initial; }) ? initial : "ssa");
})();
