export type User = {
  id: number;
  name: string;
  title: string;
};

export function findUserById(users: User[], id: number): User | undefined {
  return users.find((user) => user.id === id);
}

export function formatUserLabel(user: User): string {
  return `${user.name} (${user.title})`;
}
