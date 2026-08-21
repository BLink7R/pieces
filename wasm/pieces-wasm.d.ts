export interface OperationID {
    replica: string;
    stamp: number;
}

export interface Anchor {
    replica: string;
    stamp: number;
    offset: number;
}

export interface OpenedRange {
    begin: Anchor;
    end: Anchor;
}

export interface ClosedRange {
    begin: Anchor;
    end: Anchor;
}

export enum OperationType {
    Insert = 0,
    ParagraphHead = 1,
    InlineObject = 2,
    Delete = 3,
    RangeFormat = 4,
    ParaFormat = 5,
    Undo = 6,
    Redo = 7,
}

export interface Operation {
    replica: string;
    stamp: number;
    type: OperationType;
}

export interface Insertion extends Operation {
    type: OperationType.Insert;
    anchor: Anchor;
    str: string;
}

export interface ParagraphHeadInsert extends Operation {
    type: OperationType.ParagraphHead;
    anchor: Anchor;
}

export interface InlineObjectInsert extends Operation {
    type: OperationType.InlineObject;
    anchor: Anchor;
    id: string;
}

export interface Deletion extends Operation {
    type: OperationType.Delete;
    range: ClosedRange;
}

export interface UndoOperation extends Operation {
    type: OperationType.Undo;
    target: OperationID;
}

export interface RedoOperation extends Operation {
    type: OperationType.Redo;
    target: OperationID;
}

export type AnyOperation = Insertion | ParagraphHeadInsert | InlineObjectInsert | Deletion | UndoOperation | RedoOperation;

export class PlainText {
    constructor();
    size(): number;
    empty(): boolean;
    toString(): string;
    slice(start: number, end: number): string;
    insert(index: number, text: string): number;
    insertAnchor(anchor: Anchor, text: string): number;
    del(start: number, end: number): number;
    delAnchor(range: ClosedRange): number;
    toRange(start: number, end: number): ClosedRange;
    toAnchor(offset: number): Anchor;
    toOffset(anchor: Anchor): number;
    undo(): void;
    redo(): void;
    canUndo(): boolean;
    canRedo(): boolean;
    undoSpecific(opID: OperationID): void;
    redoSpecific(opID: OperationID): void;
    replicaID(): string;
    origin(): string;
    apply(op: AnyOperation | AnyOperation[]): void;
    frontline(): OperationID[];
    diff(frontline: OperationID[]): AnyOperation[];
    delete(): void;
}

// All positions (index/offset) and Anchor.offset are UTF-16 code units, matching DOM
// Range/Selection offsets. Insertion.str is a UTF-16 JS string.
export interface PiecesModule {
    PlainText: typeof PlainText;
    OperationType: typeof OperationType;
}

export default function createModule(options?: any): Promise<PiecesModule>;
