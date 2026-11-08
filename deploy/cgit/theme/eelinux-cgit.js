/* Adaptação progressiva das tabelas nativas; os dados do Git não são alterados. */
(function () {
  'use strict';

  function lineRanges(text) {
    var ranges = [], start = 0, end;
    while (start < text.length) {
      end = text.indexOf('\n', start);
      ranges.push({ start: start, end: end < 0 ? text.length : end, newline: end >= 0 });
      start = end < 0 ? text.length : end + 1;
    }
    return ranges;
  }

  /* A função pura também permite verificar os casos de texto sem um browser. */
  if (typeof module === 'object' && module.exports) module.exports = { lineRanges: lineRanges };
  if (typeof document === 'undefined') return;
  var cgit = document.getElementById('cgit');
  if (!cgit) return;
  cgit.tabIndex = -1;

  /* As percentagens do diffstat mantêm a escala na grelha fluida. */
  cgit.querySelectorAll('table.diffstat td.graph table[width]').forEach(function (table) {
    var width = table.getAttribute('width');
    if (/^\d+(?:\.\d+)?%$/.test(width) && parseFloat(width) <= 100) table.style.width = width;
  });

  /* As legendas vêm dos cabeçalhos existentes, incluindo secções com colspan. */
  cgit.querySelectorAll('table.list').forEach(function (table) {
    var labels = [];
    Array.from(table.rows).forEach(function (row) {
      var cells = Array.from(row.cells), column = 0;
      if (!cells.length) return;
      if (cells.every(function (cell) { return cell.tagName === 'TH'; })) {
        labels = [];
        row.classList.add('ee-list-head');
        cells.forEach(function (cell) {
          var label = cell.textContent.replace(/\s+/g, ' ').trim();
          for (var i = 0; i < cell.colSpan; i++) labels.push(label);
        });
        return;
      }
      if (cells.every(function (cell) { return !cell.textContent.trim(); })) {
        row.classList.add('ee-list-space');
        return;
      }
      cells.forEach(function (cell) {
        var label = labels[column] || '';
        cell.dataset.eeLabel = label;
        if (cell.colSpan > 1 || /^(name|description|commit message(?: \(.*\))?|clone)$/i.test(label) ||
            cell.classList.contains('logsubject') || cell.querySelector('a.ls-dir, a.ls-blob, a[rel="vcs-git"]')) {
          cell.classList.add('ee-list-wide');
        }
        column += cell.colSpan;
      });
    });
    table.classList.add('ee-list-labelled');
  });

  var narrow = window.matchMedia('(max-width: 1024px)'), sourceStates = [];

  /* Os ranges conservam os spans de um filtro de sintaxe, sem interpretar HTML. */
  function fragmentsFor(pre, ranges) {
    var walker = document.createTreeWalker(pre, NodeFilter.SHOW_TEXT), nodes = [], length = 0, node;
    while ((node = walker.nextNode())) {
      nodes.push({ node: node, start: length, end: length + node.data.length });
      length += node.data.length;
    }
    if (!nodes.length) return ranges.length ? null : [];
    function boundary(offset) {
      var low = 0, high = nodes.length - 1;
      while (low < high) {
        var middle = Math.floor((low + high) / 2);
        if (nodes[middle].end < offset) low = middle + 1;
        else high = middle;
      }
      return { node: nodes[low].node, offset: offset - nodes[low].start };
    }
    return ranges.map(function (line) {
      var start = boundary(line.start), end = boundary(line.end), range = document.createRange();
      range.setStart(start.node, start.offset);
      range.setEnd(end.node, end.offset);
      return range.cloneContents();
    });
  }

  cgit.querySelectorAll('table.blob:not(.blame)').forEach(function (table) {
    var original = table.tBodies[0], row = original && original.rows[0];
    if (!row || original.rows.length !== 1) return;
    var numbers = row.querySelector('td.linenumbers'), source = row.querySelector('td.lines');
    var pre = source && source.querySelector('pre');
    if (!numbers || !pre) return;
    var anchors = Array.from(numbers.querySelectorAll('a[id]'));
    var text = pre.textContent, ranges = lineRanges(text);
    if (!ranges.length || anchors.length !== ranges.length) return;
    var fragments = fragmentsFor(pre, ranges);
    if (!fragments) return;
    var fragmentIds = new Set(), duplicateId = false;
    fragments.forEach(function (fragment) {
      fragment.querySelectorAll('[id]').forEach(function (element) {
        if (fragmentIds.has(element.id)) duplicateId = true;
        fragmentIds.add(element.id);
      });
    });
    if (duplicateId) return;
    var wrapped = document.createElement('tbody');
    ranges.forEach(function (line, index) {
      var newRow = document.createElement('tr'), numberCell = numbers.cloneNode(false);
      var sourceCell = source.cloneNode(false), sourcePre = pre.cloneNode(false);
      numberCell.appendChild(anchors[index].cloneNode(true));
      sourcePre.appendChild(fragments[index]);
      sourceCell.appendChild(sourcePre);
      /* A quebra original fica no texto da célula, sem criar uma linha vazia. */
      if (line.newline) sourceCell.appendChild(document.createTextNode('\n'));
      newRow.appendChild(numberCell);
      newRow.appendChild(sourceCell);
      wrapped.appendChild(newRow);
    });
    if (Array.from(wrapped.querySelectorAll('td.lines')).map(function (cell) { return cell.textContent; }).join('') !== text) return;
    sourceStates.push({ table: table, original: original, wrapped: wrapped });

    /* Copiar várias linhas não acrescenta números nem quebras de apresentação. */
    table.addEventListener('copy', function (event) {
      if (!table.classList.contains('ee-wrapped-source') || !event.clipboardData) return;
      var selection = window.getSelection();
      if (!selection || selection.rangeCount !== 1 || selection.isCollapsed) return;
      function sourceCell(node) {
        var element = node.nodeType === 1 ? node : node.parentElement;
        var cell = element && element.closest('td.lines');
        return cell && cell.closest('table') === table ? cell : null;
      }
      var first = sourceCell(selection.anchorNode), last = sourceCell(selection.focusNode);
      if (!first || !last) return;
      var range = selection.getRangeAt(0), copied;
      if (first === last) copied = range.toString();
      else copied = Array.from(range.cloneContents().querySelectorAll('td.lines')).map(function (cell) { return cell.textContent; }).join('');
      event.clipboardData.setData('text/plain', copied);
      event.preventDefault();
    });
  });

  function updateSources() {
    sourceStates.forEach(function (state) {
      var desired = narrow.matches ? state.wrapped : state.original;
      if (state.table.tBodies[0] !== desired) state.table.replaceChild(desired, state.table.tBodies[0]);
      state.table.classList.toggle('ee-wrapped-source', narrow.matches);
    });
    /* Une liaison directe vers une ligne reste placée sur la bonne ligne. */
    if (narrow.matches && /^#n\d+$/.test(window.location.hash)) {
      var target = document.getElementById(window.location.hash.slice(1));
      if (target) target.scrollIntoView({ block: 'nearest' });
    }
  }
  updateSources();
  if (narrow.addEventListener) narrow.addEventListener('change', updateSources);
  else narrow.addListener(updateSources);
})();
