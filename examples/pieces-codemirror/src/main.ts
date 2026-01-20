import { EditorState, Annotation, Transaction, Prec } from "@codemirror/state";
import { EditorView, keymap } from "@codemirror/view";
import { basicSetup } from "codemirror";
import { defaultKeymap } from "@codemirror/commands";
import type { PiecesModule, PlainText, OperationID, Anchor, Deletion, Insertion, AnyOperation } from "../public/pieces-wasm.d.ts";

// @ts-ignore
import createPiecesModule from "../public/pieces-wasm.mjs";

declare global {
  interface Window {
    pieces: PiecesModule;
  }
}

const remoteAnnotation = Annotation.define<boolean>();

interface Port {
  id: number;
  crdt: PlainText;
  view: EditorView;
  flush: () => void;
}

const ports: Port[] = [];

// Helper to load the module once
async function loadPiecesModule(): Promise<PiecesModule> {
  const Module = await createPiecesModule({
    locateFile: (path: string) => {
      if (path.endsWith(".wasm")) {
        return "/pieces-wasm.wasm";
      }
      return path;
    }
  }) as PiecesModule;
  return Module;
}

// Function to create a new editor instance with its own CRDT document
function createEditor(parentElement: HTMLElement, Module: PiecesModule) {
  const crdt: PlainText = new Module.PlainText();

  // Initial text with a dynamic ID to distinguish them
  const editorId = ports.length + 1;
  if (editorId === 1) {
    crdt.insert(0, `Hello CRDT! Editor ${editorId}`);
  }

  let pendingInsert: { from: number; text: string } | null = null;
  let debounceTimer: number | null = null;

  const flush = () => {
    if (debounceTimer) {
      clearTimeout(debounceTimer);
      debounceTimer = null;
    }
    if (pendingInsert) {
      console.log(`[Editor ${editorId}] Flushing: "${pendingInsert.text}"`);
      crdt.insert(pendingInsert.from, pendingInsert.text);
      pendingInsert = null;
      sync();
    }
  };

  const sync = () => {
    // console.log(`[Editor ${editorId}] Syncing...`);
    let pulled = false;

    for (const port of ports) {
      if (port.id === editorId) continue;

      // Ensure that remote port flushed its own buffer, if any?
      // Actually, we just want to ensure we don't apply ops if remote is typing.
      // But typically we force remote to flush before applying OUR ops.
      port.flush();

      const otherFrontline = port.crdt.frontline();
      const ops = crdt.diff(otherFrontline);
      console.log(`[Editor ${editorId}] -> Diffed ${ops.length} ops for Editor ${port.id}`);
      console.log(ops);

      // push
      if (ops.length > 0) {
        const anchor = port.crdt.toAnchor(port.view.state.selection.main.head);
        port.crdt.apply(ops);
        port.view.dispatch({
          changes: { from: 0, to: port.view.state.doc.length, insert: port.crdt.toString() },
          selection: { anchor: port.crdt.toOffset(anchor) },
          annotations: [remoteAnnotation.of(true)]
        });
        // console.log(`[Editor ${editorId}]  -> Sent ${ops.length} ops to Editor ${port.id}`);
      }

      // Pull
      const myFrontline = crdt.frontline();
      const pullOps = port.crdt.diff(myFrontline);
      if (pullOps.length > 0) {
        console.log(`[Editor ${port.id}] -> Diffed ${pullOps.length} ops for Editor ${editorId}`);
        console.log(pullOps);
        crdt.apply(pullOps);
        pulled = true;
      }
    }

    if (pulled) {
      const anchor = crdt.toAnchor(view.state.selection.main.head);
      view.dispatch({
        changes: { from: 0, to: view.state.doc.length, insert: crdt.toString() },
        selection: { anchor: crdt.toOffset(anchor) },
        annotations: [remoteAnnotation.of(true)]
      });
    }
  };

  const updateListener = EditorView.updateListener.of((update) => {
    // 1. Prevent loop
    if (update.transactions.some(tr => tr.annotation(remoteAnnotation))) {
      return;
    }

    // 2. No document changes
    if (!update.docChanged) {
      return;
    }

    update.changes.iterChanges((fromA, toA, fromB, toB, inserted) => {
      const len = toA - fromA;
      const text = inserted.toString();

      // has deletion
      if (len > 0) {
        flush();
        crdt.del(fromA, toA);
      }

      if (text.length > 0) {
        // Check for contiguity
        if (pendingInsert && fromA === (pendingInsert.from + pendingInsert.text.length)) {
          // Keep buffering.
          pendingInsert.text += text;
          if (debounceTimer) clearTimeout(debounceTimer);
          debounceTimer = setTimeout(flush, 500) as unknown as number; // 500ms
          return;
        } else {
          // Flush old, start new.
          flush();
          pendingInsert = { from: fromA, text: text };
          if (debounceTimer) clearTimeout(debounceTimer);
          debounceTimer = setTimeout(() => {
            console.log(`[Editor ${editorId}] timeout`);
            flush();
          }, 500) as unknown as number;
          return;
        }
      }

      sync();
    });

  });

  // Custom Keymap to intercept Undo/Redo
  const customKeymap = keymap.of([
    {
      key: "Mod-z",
      run: () => {
        flush(); // Flush buffer before undo
        console.log(`[Editor ${editorId}] Intercepted Undo`);
        const anchor = crdt.toAnchor(view.state.selection.main.head);
        crdt.undo();

        // Sync new state to CM view
        view.dispatch({
          changes: { from: 0, to: view.state.doc.length, insert: crdt.toString() },
          selection: { anchor: crdt.toOffset(anchor) },
          annotations: [remoteAnnotation.of(true)]
        });
        sync();
        return true;
      }
    },
    {
      key: "Mod-y",
      run: () => {
        flush();
        console.log(`[Editor ${editorId}] Intercepted Redo`);
        const anchor = crdt.toAnchor(view.state.selection.main.head);
        crdt.redo();
        view.dispatch({
          changes: { from: 0, to: view.state.doc.length, insert: crdt.toString() },
          selection: { anchor: crdt.toOffset(anchor) },
          annotations: [remoteAnnotation.of(true)]
        });
        sync();
        return true;
      }
    },
    {
      key: "Mod-Shift-z",
      run: () => {
        flush();
        console.log(`[Editor ${editorId}] Intercepted Redo (Mac)`);
        const anchor = crdt.toAnchor(view.state.selection.main.head);
        crdt.redo();
        view.dispatch({
          changes: { from: 0, to: view.state.doc.length, insert: crdt.toString() },
          selection: { anchor: crdt.toOffset(anchor) },
          annotations: [remoteAnnotation.of(true)]
        });
        sync();
        return true;
      }
    },
    ...defaultKeymap
  ]);

  // prevent browser native undo/redo
  const preventBrowserUndo = EditorView.domEventHandlers({
    beforeinput(e, view) {
      if (e.inputType === "historyUndo" || e.inputType === "historyRedo") {
        console.log(`[Editor ${editorId}] beforeinput: ${e.inputType}`);
        e.preventDefault();
        return true;
      }
      return false;
    }
  });

  const view = new EditorView({
    doc: crdt.toString(),
    extensions: [
      preventBrowserUndo,
      Prec.highest(customKeymap),
      basicSetup,
      updateListener
    ],
    parent: parentElement
  });

  const port: Port = { id: editorId, crdt, view, flush };
  ports.push(port);

  if (editorId !== 1) {
    sync();
  }

  return port;
}

async function init() {
  const status = document.getElementById("status")!;
  const editorsContainer = document.getElementById("editors-container")!;
  const addEditorBtn = document.getElementById("add-editor-btn")!;

  // 1. Load and mount module globally
  const Module = await loadPiecesModule();
  window.pieces = Module;
  status.innerText = "WASM Loaded";

  // 2. Setup the initial editor
  const firstEditorEl = document.getElementById("editor-1");
  if (firstEditorEl) {
    createEditor(firstEditorEl, Module);
  }

  // 3. Enable "Add Editor" button
  addEditorBtn.addEventListener("click", () => {
    const newEditorEl = document.createElement("div");
    newEditorEl.className = "editor-wrapper";
    editorsContainer.appendChild(newEditorEl);
    createEditor(newEditorEl, Module);
  });
}

init();
