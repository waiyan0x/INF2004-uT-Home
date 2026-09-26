/** Minimal observable store.
 *
 * Views never mutate state directly: actions call `set`, every subscriber then
 * re-renders from the new snapshot. This keeps the render path one-directional
 * and makes each view a pure function of state.
 */

export function createStore(initialState) {
  let state = { ...initialState };
  const listeners = new Set();

  return {
    get() {
      return state;
    },
    set(patch) {
      state = { ...state, ...patch };
      listeners.forEach((listener) => listener(state));
      return state;
    },
    subscribe(listener) {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
  };
}

export const store = createStore({
  profile: null,      // parsed profile currently under review
  candidate: null,    // validated candidate awaiting a decision
  reviewed: null,     // candidate after approve or reject
  health: null,       // normalised health snapshot, when loaded
  healthSource: null, // sample fixture or an imported file
  healthError: null,
  validation: null,   // { tone, title, text, errors }
  review: null,       // { tone, title, text }
});
