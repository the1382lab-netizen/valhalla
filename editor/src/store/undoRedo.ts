import { create } from 'zustand';

export interface UndoRedoCommand {
  type: string;
  section: string;
  path: string;
  oldValue: any;
  newValue: any;
  do: () => void;
  undo: () => void;
}

interface UndoRedoState {
  // Stack management
  undoStack: UndoRedoCommand[];
  redoStack: UndoRedoCommand[];

  // Command history methods
  push: (command: UndoRedoCommand) => void;
  undo: () => void;
  redo: () => void;
  clear: () => void;

  // Getters
  canUndo: () => boolean;
  canRedo: () => boolean;
  getUndoCount: () => number;
  getRedoCount: () => number;
}

const MAX_STACK_DEPTH = 100;

export const useUndoRedoStore = create<UndoRedoState>((set, get) => ({
  undoStack: [],
  redoStack: [],

  push: (command) => {
    set((state) => {
      // Execute the command
      command.do();

      // Add to undo stack
      const newUndoStack = [command, ...state.undoStack];

      // Limit stack depth
      if (newUndoStack.length > MAX_STACK_DEPTH) {
        newUndoStack.pop();
      }

      // Clear redo stack when a new command is pushed
      return {
        undoStack: newUndoStack,
        redoStack: [],
      };
    });
  },

  undo: () => {
    set((state) => {
      if (state.undoStack.length === 0) return state;

      const [command, ...remainingUndoStack] = state.undoStack;

      // Execute the undo
      command.undo();

      // Move command to redo stack
      return {
        undoStack: remainingUndoStack,
        redoStack: [command, ...state.redoStack],
      };
    });
  },

  redo: () => {
    set((state) => {
      if (state.redoStack.length === 0) return state;

      const [command, ...remainingRedoStack] = state.redoStack;

      // Execute the do
      command.do();

      // Move command back to undo stack
      return {
        undoStack: [command, ...state.undoStack],
        redoStack: remainingRedoStack,
      };
    });
  },

  clear: () => {
    set({
      undoStack: [],
      redoStack: [],
    });
  },

  canUndo: () => {
    return get().undoStack.length > 0;
  },

  canRedo: () => {
    return get().redoStack.length > 0;
  },

  getUndoCount: () => {
    return get().undoStack.length;
  },

  getRedoCount: () => {
    return get().redoStack.length;
  },
}));
