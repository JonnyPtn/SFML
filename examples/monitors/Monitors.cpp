////////////////////////////////////////////////////////////
// Headers
////////////////////////////////////////////////////////////
#include <SFML/Graphics/Font.hpp>
#include <SFML/Window/Monitor.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Text.hpp>

////////////////////////////////////////////////////////////
int main()
{
    sf::RenderWindow window({{400, 400}}, "Monitors");

    const sf::Font font{"resources/tuffy.ttf"};
    sf::Text defaultText(font);
    defaultText.setString("This is the default monitor");
    sf::Text notDefaultText(font);
    notDefaultText.setString("This is not the default monitor");
    sf::Text name(font);
    name.setPosition({0, 100});

    while (window.isOpen())
    {
        while (auto event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            {
                window.close();
            }
            else if (const auto keyPress = event->getIf<sf::Event::KeyPressed>())
            {
                if (keyPress->scancode == sf::Keyboard::Scan::Enter)
                {
                    
                }
            }
        }

        window.clear();
        auto monitor = window.getMonitor();
        name.setString(monitor.getName());
        if (monitor.isDefault())
        {
            window.draw(defaultText);
        }
        else
        {
            window.draw(notDefaultText);
        }
        window.draw(name);
        window.display();
    }
}
