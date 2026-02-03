type User = {
  id: number;
  name: string;
};

const users: User[] = [
  { id: 1, name: "Ada" },
  { id: 2, name: "Linus" },
];

function findUser(id: number): User | undefined {
  return users.find((u) => u.id === id);
}

const first = findUser(1);
if (first) {
  console.log(first.name);
}
