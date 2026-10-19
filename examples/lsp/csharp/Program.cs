using CSharpExample;

var greeter = new Greeter("Hello");

// Go to definition on Greet to jump to Greeter.cs.
Console.WriteLine(greeter.Greet("Ada"));
Console.WriteLine(greeter.Greet("Linus"));

// Completion: on a new line, type greeter. and choose Greet.
// Diagnostics: uncomment this line, then inspect the error with Space e.
// Console.WriteLine(greeter.Greet(42));
