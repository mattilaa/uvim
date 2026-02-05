export function setMessage(target, text) {
  if (!target) return;
  target.textContent = text;
}

export function formatCount(value) {
  return `Clicks: ${value}`;
}

export function attachClickCounter(button) {
  const state = { value: 0 };
  if (!button) return state;
  button.addEventListener("click", () => {
    state.value += 1;
  });
  return state;
}
