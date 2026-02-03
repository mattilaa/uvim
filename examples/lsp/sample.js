const button = document.getElementById("btn");
const lead = document.querySelector(".lead");

function setMessage(text) {
  if (!lead) return;
  lead.textContent = text;
}

if (button) {
  button.addEventListener("click", () => {
    setMessage("Button clicked!");
  });
}
