import { type User, findUserById, formatUserLabel } from "./models.ts";

const users: User[] = [
  { id: 1, name: "Ada", title: "Engineer" },
  { id: 2, name: "Linus", title: "Architect" },
];

const first = findUserById(users, 1);
if (first) {
  console.log(formatUserLabel(first));
}
