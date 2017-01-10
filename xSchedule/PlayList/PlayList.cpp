#include "PlayList.h"
#include "PlayListDialog.h"
#include "PlayListStep.h"
#include "../Schedule.h"

#include <wx/xml/xml.h>
#include <log4cpp/Category.hh>

PlayList::PlayList(wxXmlNode* node)
{
    _forceNextStep = "";
    _jumpToEndStepsAtEndOfCurrentStep = false;
    _priority = 0;
    _lastLoop = false;
    _looping = false;
    _random = false;
    _loopStep = false;
    _pauseTime.Set((time_t)0);
    _stopAtEndOfCurrentStep = false;
    _dirty = false;
    _currentStep = nullptr;
    Load(node);
}

PlayList::PlayList(const PlayList& playlist)
{
    _forceNextStep = "";
    _jumpToEndStepsAtEndOfCurrentStep = false;
    _loopStep = false;
    _lastLoop = false;
    _random = false;
    _looping = false;
    _dirty = false;
    _currentStep = nullptr;
    _stopAtEndOfCurrentStep = false;
    _pauseTime.Set((time_t)0);
    _firstOnlyOnce = playlist._firstOnlyOnce;
    _lastOnlyOnce = playlist._lastOnlyOnce;
    _name = playlist._name;
    _priority = playlist._priority;
    for (auto it = playlist._steps.begin(); it != playlist._steps.end(); ++it)
    {
        _steps.push_back(new PlayListStep(**it));
    }
    // dont copy the schedule as it wont be needed
}

PlayList::PlayList()
{
    _forceNextStep = "";
    _jumpToEndStepsAtEndOfCurrentStep = false;
    _priority = 0;
    _loopStep = false;
    _lastLoop = false;
    _random = false;
    _looping = false;
    _pauseTime.Set((time_t)0);
    _dirty = false;
    _currentStep = nullptr;
    _firstOnlyOnce = false;
    _lastOnlyOnce = false;
    _name = "";
    _stopAtEndOfCurrentStep = false;
}

PlayList::~PlayList()
{
    while (_steps.size() > 0)
    {
        auto toremove = _steps.front();
        _steps.remove(toremove);
        delete toremove;
    }

    while (_schedules.size() > 0)
    {
        auto toremove = _schedules.front();
        _schedules.remove(toremove);
        delete toremove;
    }
}


wxXmlNode* PlayList::Save()
{
    wxXmlNode* res = new wxXmlNode(nullptr, wxXML_ELEMENT_NODE, "PlayList");

    if (_firstOnlyOnce)
    {
        res->AddAttribute("FirstOnce", "TRUE");
    }

    if (_lastOnlyOnce)
    {
        res->AddAttribute("LastOnce", "TRUE");
    }

    res->AddAttribute("Name", _name);

    for (auto it = _steps.begin(); it != _steps.end(); ++it)
    {
        res->AddChild((*it)->Save());
    }

    for (auto it = _schedules.begin(); it != _schedules.end(); ++it)
    {
        res->AddChild((*it)->Save());
    }

    _dirty = false;

    return res;
}

void PlayList::Load(wxXmlNode* node)
{
    _steps.clear();
    _schedules.clear();
    _firstOnlyOnce = (node->GetAttribute("FirstOnce", "FALSE") == "TRUE");
    _lastOnlyOnce = (node->GetAttribute("LastOnce", "FALSE") == "TRUE");
    _name = node->GetAttribute("Name", "");

    for (wxXmlNode* n = node->GetChildren(); n != nullptr; n = n->GetNext())
    {
        if (n->GetName() == "PlayListStep")
        {
            _steps.push_back(new PlayListStep(n));
        }
        else if (n->GetName() == "Schedule")
        {
            _schedules.push_back(new Schedule(n));
        }
    }
}

PlayList* PlayList::Configure(wxWindow* parent)
{
    PlayListDialog dlg(parent, this);

    if (dlg.ShowModal() == wxID_CANCEL)
    {
        return nullptr;
    }

    return this;
}

bool PlayList::IsDirty() const
{
    bool res = _dirty;

    auto it = _steps.begin();
    while (!res && it != _steps.end())
    {
        res = res || (*it)->IsDirty();
        ++it;
    }

    auto it2 = _schedules.begin();
    while (!res && it2 != _schedules.end())
    {
        res = res || (*it2)->IsDirty();
        ++it2;
    }

    return res;
}

void PlayList::AddSchedule(Schedule* schedule)
{
    _schedules.push_back(schedule);
    _dirty = true;
}

void PlayList::AddStep(PlayListStep* item, int pos)
{
    std::list<PlayListStep*> newsteps;

    bool inserted = false;
    int i = 0;
    for (auto it = _steps.begin(); it != _steps.end(); ++it)
    {
        if (i == pos)
        {
            newsteps.push_back(item);
            inserted = true;
        }
        newsteps.push_back(*it);
        i++;
    }

    if (!inserted)
    {
        newsteps.push_back(item);
    }

    _steps = newsteps;
}

void PlayList::ClearDirty()
{
    _dirty = false;

    for (auto it = _steps.begin(); it != _steps.end(); ++it)
    {
        (*it)->ClearDirty();
    }

    for (auto it = _schedules.begin(); it != _schedules.end(); ++it)
    {
        (*it)->ClearDirty();
    }
}

void PlayList::RemoveStep(PlayListStep* step)
{
    _steps.remove(step);
    _dirty = true;
}

void PlayList::RemoveSchedule(Schedule* schedule)
{
    _schedules.remove(schedule);
    _dirty = true;
}

void PlayList::MoveStepAfterStep(PlayListStep* movethis, PlayListStep* afterthis)
{
    if (movethis == afterthis) return;

    if (afterthis == nullptr)
    {
        RemoveStep(movethis);
        _steps.push_front(movethis);
    }
    else
    {
        RemoveStep(movethis);
        int pos = GetPos(afterthis);
        if (pos == -1)
        {
            wxASSERT(false);
            _steps.push_back(movethis);
        }
        else
        {
            AddStep(movethis, pos + 1);
        }
    }
}

int PlayList::GetPos(PlayListStep* step)
{
    int i = 0;
    for (auto it = _steps.begin(); it != _steps.end(); ++it)
    {
        if (*it == step)
        {
            return i;
        }
        i++;
    }

    return -1;
}

bool PlayList::Frame(wxByte* buffer, size_t size)
{
    if (_currentStep != nullptr)
    {
        if (IsPaused()) return false;

        // This returns true if everything is done
        if (_currentStep->Frame(buffer, size))
        {
            return !JumpToNextStep();
        }
    }

    return false;
}

bool PlayList::IsRunning() const
{
    return _currentStep != nullptr;
}

void PlayList::Start(bool loop, bool random)
{
    if (IsRunning()) return;
    if (_steps.size() == 0) return;

    _looping = loop;
    _random = random;

    static log4cpp::Category &logger_base = log4cpp::Category::getInstance(std::string("log_base"));
    logger_base.info("Playlist %s starting to play.", (const char*)GetName().c_str());

    _forceNextStep = "";
    _loopStep = false;
    _stopAtEndOfCurrentStep = false;
    _jumpToEndStepsAtEndOfCurrentStep = false;
    _lastLoop = false;
    _stopAtEndOfCurrentStep = false;
    if (_random && !_firstOnlyOnce)
    {
        _currentStep = GetRandomStep();
    }
    else
    {
        _currentStep = _steps.front();
    }
    _currentStep->Start();
}

void PlayList::Stop()
{
    if (!IsRunning()) return;

    static log4cpp::Category &logger_base = log4cpp::Category::getInstance(std::string("log_base"));
    logger_base.info("Playlist %s stopping.", (const char*)GetName().c_str());

    _currentStep->Stop();
    _currentStep = nullptr;
}

PlayListStep* PlayList::GetNextStep() const
{
    if (_stopAtEndOfCurrentStep) return nullptr;
    if (_currentStep == nullptr) return nullptr;

    if (_forceNextStep != "")
    {
        for (auto it = _steps.begin(); it != _steps.end(); ++it)
        {
            if ((*it)->GetNameNoTime() == _forceNextStep)
            {
                return *it;
            }
        }
    }

    if (_loopStep)
    {
        return _currentStep;
    }

    auto last = _steps.end();
    if (_steps.size() > 1)
    {
        auto it = _steps.end();
        --it;
        last = it;
    }

    if (_random && !_lastLoop)
    {
        return GetRandomStep();
    }

    for (auto it = _steps.begin(); it != _steps.end(); ++it)
    {
        if (*it == _currentStep)
        {
            ++it;
            if (_jumpToEndStepsAtEndOfCurrentStep)
            {
                if (_steps.size() == 1) return nullptr;

                if (_lastOnlyOnce)
                {
                    return _steps.back();
                }
                else
                {
                    return nullptr;
                }
            }
            else if (IsLooping() && it == last && !_lastLoop)
            {
                if (_firstOnlyOnce)
                {
                    if (_steps.size() == 1) return nullptr;
                    auto it2 = _steps.begin();
                    ++it2;
                    return *it2;
                }

                return _steps.front();
            }
            else if (it == _steps.end())
            {
                return nullptr;
            }
            else
            {
                return (*it);
            }
        }
    }
    return nullptr;
}

PlayListStep* PlayList::GetPriorStep() const
{
    if (_stopAtEndOfCurrentStep) return nullptr;
    if (_currentStep == nullptr) return nullptr;

    PlayListStep* last = _steps.back();
    for (auto it = _steps.begin(); it != _steps.end(); ++it)
    {
        if (*it == _currentStep)
        {
            return last;
        }
        last = *it;
    }

    return nullptr;
}

size_t PlayList::GetLengthMS() const
{
    size_t length = 0;
    for (auto it = _steps.begin(); it != _steps.end(); ++it)
    {
        length += (*it)->GetLengthMS();
    }

    return length;
}

bool PlayList::IsPaused() const
{
    wxDateTime zero((time_t)0);
    return (_pauseTime != zero);
}

void PlayList::Pause()
{
    if (_currentStep == nullptr) return;

    if (IsPaused())
    {
        _currentStep->AdjustTime(wxDateTime::Now() - _pauseTime);
        _pauseTime = wxDateTime((time_t)0);
    }
    else
    {
        _pauseTime = wxDateTime::Now();
    }
}

bool PlayList::JumpToPriorStep()
{
    bool success = false;

    if (_currentStep == nullptr) return false;

    _currentStep->Stop();
    _currentStep = GetPriorStep();

    if (_currentStep == nullptr) return false;

    _currentStep->Start();

    return success;
}

bool PlayList::JumpToNextStep()
{
    bool success = true;

    _loopStep = false;
    _forceNextStep = "";

    if (_currentStep == nullptr) return false;

    _currentStep->Stop();
    _currentStep = GetNextStep();

    if (_currentStep == nullptr) return false;

    _currentStep->Start();

    return success;
}

void PlayList::RestartCurrentStep()
{
    _forceNextStep = "";
    _loopStep = false;
    if (_currentStep != nullptr) _currentStep->Restart();
}

bool PlayList::JumpToStep(const std::string& step)
{
    bool success = true;

    _lastLoop = false;
    _loopStep = false;
    _forceNextStep = "";

    if (_currentStep != nullptr && _currentStep->GetName() == step)
    {
        _currentStep->Restart();
        return success;
    }

    _currentStep->Stop();

    _currentStep = GetStep(step);
    if (_currentStep == nullptr)
    {
        return false;
    }

    _currentStep->Start();

    return success;
}

bool PlayList::JumpToEndStepsAtEndOfCurrentStep()
{
    _forceNextStep = "";
    _jumpToEndStepsAtEndOfCurrentStep = true;
    _lastLoop = true;
    _loopStep = false;

    return (_steps.size() > 1 && _lastOnlyOnce);
}

PlayListStep* PlayList::GetStep(const std::string& step)
{
    for (auto it = _steps.begin(); it != _steps.end(); ++it)
    {
        if ((*it)->GetNameNoTime() == step) return (*it);
    }

    return nullptr;
}

PlayListStep* PlayList::GetRandomStep() const
{
    int offset = 0;
    int count = _steps.size();
    if (_firstOnlyOnce) 
    {
        count--;
        offset++;
    }
    if (_lastOnlyOnce) count--;

    if (count < 3)
    {
        return GetNextStep();
    }
    else
    {
        wxASSERT(count + offset <= _steps.size());

        int selected = rand() % count;
        auto it = _steps.begin();
        std::advance(it, selected + offset);

        if (it == _steps.end()) return nullptr;

        // we dont want the same step so try again
        if (*it == _currentStep)
        {
            return GetRandomStep();
        }

        return *it;
    }
}

bool PlayList::LoopStep(const std::string step)
{
    if (_currentStep == nullptr || _currentStep->GetName() != step)
    {
        JumpToStep(step);
    }

    _forceNextStep = "";
    _loopStep = true;
    _lastLoop = false;
    _looping = false;

    return true;
}