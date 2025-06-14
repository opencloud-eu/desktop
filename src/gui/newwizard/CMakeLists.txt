add_library(newwizard STATIC
    enums.cpp

    pages/abstractsetupwizardpage.h
    pages/abstractsetupwizardpage.cpp

    pages/serverurlsetupwizardpage.ui
    pages/serverurlsetupwizardpage.h
    pages/serverurlsetupwizardpage.cpp

    pages/oauthcredentialssetupwizardpage.h
    pages/oauthcredentialssetupwizardpage.cpp

    pages/accountconfiguredwizardpage.ui
    pages/accountconfiguredwizardpage.h
    pages/accountconfiguredwizardpage.cpp

    setupwizardwidget.ui
    setupwizardwidget.h
    setupwizardwidget.cpp

    setupwizardcontroller.h
    setupwizardcontroller.cpp

    setupwizardaccountbuilder.cpp
    setupwizardaccountbuilder.h

    navigation.cpp
    navigation.h

    jobs/resolveurljobfactory.cpp
    jobs/resolveurljobfactory.h

    jobs/discoverwebfingerservicejobfactory.cpp
    jobs/discoverwebfingerservicejobfactory.h

    jobs/webfingeruserinfojobfactory.cpp
    jobs/webfingeruserinfojobfactory.h



    states/abstractsetupwizardstate.cpp
    states/serverurlsetupwizardstate.cpp
    states/oauthcredentialssetupwizardstate.cpp
    states/accountconfiguredsetupwizardstate.cpp

    setupwizardcontext.cpp
)
target_link_libraries(newwizard PUBLIC Qt::Widgets libsync OpenCloudResources)
set_target_properties(newwizard PROPERTIES AUTOUIC ON AUTORCC ON)
target_include_directories(newwizard PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
apply_common_target_settings(newwizard)
