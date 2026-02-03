import { formatCount, attachClickCounter, setMessage } from "./utils.js";

const button = document.getElementById("btn");
const lead = document.querySelector(".lead");
const status = document.getElementById("status");

const counter = attachClickCounter(button);

if (button) {
  button.addEventListener("click", () => {
    setMessage(lead, "Button clicked!");
    if (status) {
      status.textContent = formatCount(counter.value);
    }
  });
}
