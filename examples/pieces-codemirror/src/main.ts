import { EditorState, Annotation, Transaction } from "@codemirror/state";
import { EditorView, keymap } from "@codemirror/view";
import { basicSetup } from "codemirror";
import { defaultKeymap } from "@codemirror/commands";
// @ts-ignore
import createPiecesModule from "../public/pieces-wasm.mjs";

const remoteAnnotation = Annotation.define<boolean>();

async function init() {
  const status = document.getElementById("status")!;
  
  const Module = await createPiecesModule({
    locateFile: (path: string) => {
      if (path.endsWith(".wasm")) {
        return "/pieces-wasm.wasm";
      }
      return path;
    }
  });

  status.innerText = "WASM Loaded";

  const crdt = new Module.PlainText();

  console.log()

  // Initial text
  crdt.insert(0, "Hello CRDT!");

  const updateListener = EditorView.updateListener.of((update) => {
    if (update.docChanged) {
      if (update.transactions.some(tr => tr.annotation(remoteAnnotation))) {
          return;
      }
      
      let offset = 0;
      update.changes.iterChanges((fromA, toA, fromB, toB, inserted) => {
          const len = toA - fromA;
          if (len > 0) {
              crdt.del(fromA + offset, toA + offset);
              const op = crdt.getLastOp();
              console.log("Generated Op (Delete):", op);
          }
          if (inserted.length > 0) {
              crdt.insert(fromA + offset, inserted.toString());
              const op = crdt.getLastOp();
              console.log("Generated Op (Insert):", op);
          }
          offset += (inserted.length - len);
      });
    }
  });

  const view = new EditorView({
    doc: crdt.toString(),
    extensions: [
      basicSetup,
      keymap.of(defaultKeymap),
      updateListener
    ],
    parent: document.getElementById("editor")!
  });

  // Simulate Remote Sync
  document.getElementById("sync-btn")!.addEventListener("click", () => {
    const lastOp = crdt.getLastOp();
    if (lastOp) {
        console.log("Last Op:", lastOp);
        try {
            const op = JSON.parse(lastOp);
            // Change replica ID to simulate another user
            op.replica = "00000000-0000-0000-0000-000000000002";
            op.stamp = op.stamp + 1000 + Math.floor(Math.random() * 1000);
            
            if (op.type === "insert") {
                op.text += " (Copy)";
            }
            
            const newJson = JSON.stringify(op);
            console.log("Applying remote op:", newJson);
            crdt.applyOp(newJson);
            
            // Sync to CM by replacing content
            const newDoc = crdt.toString();
            view.dispatch({
                changes: { from: 0, to: view.state.doc.length, insert: newDoc },
                annotations: [remoteAnnotation.of(true)]
            });
        } catch (e) {
            console.error("Error parsing/applying op:", e);
        }
    } else {
        console.log("No ops yet. Type something first.");
        alert("Type something in the editor first to generate an op, then click this button to replay it as a remote user.");
    }
  });
}

init();
