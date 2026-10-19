namespace CSharpExample;

public sealed class Greeter
{
    public string Greeting { get; }

    public Greeter(string greeting)
    {
        Greeting = greeting;
    }

    // Find references on Greet to see both calls in Program.cs.
    public string Greet(string name)
    {
        return $"{Greeting}, {name}!";
    }
}
