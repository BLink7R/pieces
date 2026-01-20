export interface OperationID {
    replica: string;
    stamp: number;
}

export interface Anchor {
    replica: string;
    stamp: number;
    pos: number;
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
    Delete = 1,
    Format = 2,
    Undo = 3,
    Redo = 4,
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

export type AnyOperation = Insertion | Deletion | UndoOperation | RedoOperation;

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

export interface PiecesModule {
    PlainText: typeof PlainText;
    OperationType: typeof OperationType;
}

export default function createModule(options?: any): Promise<PiecesModule>;
