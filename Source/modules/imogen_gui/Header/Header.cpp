
namespace Imogen
{
Header::Header (State& stateToUse)
	: state (stateToUse)
{
	gui::addAndMakeVisible (this, logo, inputIcon, outputLevel, scale, keyboardButton);
	// presetBar
}

void Header::paint (juce::Graphics&)
{
}

void Header::resized()
{
	// logo, keyboardButton, input icon, outputLevel, presetBar, scale
}

}  // namespace Imogen
